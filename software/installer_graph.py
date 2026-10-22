"""Fail-closed parsers for explicitly supported action-closure semantics.

Not a syscall tracer. Unsupported linker-script language is rejected, never
silently skipped. Actual SCons/Python/Node consumption still needs instrumentation.
"""
from pathlib import PurePosixPath
import re
import shlex
from installer_sdk_verify import require, hexpin


def normalized(path, cwd):
    require(type(path) is str and bool(path) and not any(c in path for c in "\0\r\n"), "INVALID_INPUT_PATH")
    p = PurePosixPath(path)
    if not p.is_absolute(): p = PurePosixPath(cwd) / p
    require(".." not in p.parts and str(p) == (path if path.startswith("/") else str(PurePosixPath(cwd) / path)), "UNNORMALIZED_INPUT_PATH")
    return str(p)


def make_words(text):
    # GNU make escaping, not shell quoting: escaped spaces/#/: and $$ literals.
    words, word, i = [], [], 0
    while i < len(text):
        c = text[i]; i += 1
        if c == "\\":
            require(i < len(text) and text[i] in " \\#:\t", "UNSUPPORTED_MAKE_ESCAPE")
            word.append(text[i]); i += 1
        elif c == "$":
            require(i < len(text) and text[i] == "$", "MAKE_EXPANSION_FORBIDDEN")
            word.append("$"); i += 1
        elif c == "#":
            break
        elif c.isspace():
            if word: words.append("".join(word)); word = []
        else: word.append(c)
    if word: words.append("".join(word))
    return words


def depfile(data):
    text = data.decode("utf-8").replace("\\\n", " ")
    rules = {}
    for line in text.splitlines():
        if not line.strip(): continue
        # Locate an unescaped colon. Windows drive names are not supported.
        match = re.search(r"(?<!\\):", line)
        require(match is not None, "MALFORMED_DEPFILE")
        targets = make_words(line[:match.start()])
        inputs = make_words(line[match.end():])
        require(len(targets) == 1 and targets[0] not in rules, "AMBIGUOUS_DEP_TARGET")
        rules[targets[0]] = inputs
    require(bool(rules) and any(rules.values()), "INCOMPLETE_DEPFILE")
    # Empty rules are allowed only as -MP phony rules for actual dependencies.
    all_inputs = {p for values in rules.values() for p in values}
    require(all(values or target in all_inputs for target, values in rules.items()), "INCOMPLETE_DEPFILE")
    return rules


class LinkSemantics:
    def __init__(self, read, cwd):
        self.read = read
        self.cwd = normalized(cwd, "/")
        self.search = []
        self.active_scripts = set()

    def expand(self, args, active=(), budget=100000):
        require(len(active) <= 64 and len(args) <= budget, "RESPONSE_LIMIT")
        result = []
        for arg in args:
            require(type(arg) is str and bool(arg), "EMPTY_ARGUMENT")
            if arg.startswith("@"):
                path = normalized(arg[1:], self.cwd)
                require(path not in active, "CYCLIC_RESPONSE")
                result.extend(self.expand(shlex.split(self.read(path).decode()), active + (path,), budget - len(result)))
            else: result.append(arg)
            require(len(result) <= budget, "RESPONSE_LIMIT")
        return result

    def resolve(self, token, base, library=False):
        if token.startswith("-l"):
            require(len(token) > 2, "EMPTY_LIBRARY")
            token = "lib" + token[2:] + ".a"
            library = True
        if token.startswith("/"):
            path = normalized(token, self.cwd)
            self.read(path)
            return path
        roots = self.search if library else [base, self.cwd] + self.search
        candidates = []
        for root in roots:
            path = normalized(token, root)
            try: self.read(path)
            except (ValueError, OSError): continue
            if path not in candidates: candidates.append(path)
        require(len(candidates) == 1, "MISSING_OR_AMBIGUOUS_SCRIPT_INPUT: " + token)
        return candidates[0]

    def script(self, path):
        path = normalized(path, self.cwd)
        require(path not in self.active_scripts and len(self.active_scripts) < 64, "CYCLIC_SCRIPT")
        self.active_scripts.add(path)
        try:
            text = self.read(path).decode()
            # Comments must be terminated; do not accidentally hide directives.
            require(text.count("/*") == text.count("*/"), "MALFORMED_SCRIPT_COMMENT")
            text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
            # Qualified minimal layout grammar. File-selecting directives are
            # still parsed by the ordinary resolver; no opaque SECTIONS skip.
            match=re.search(r'\bSECTIONS\s*\{',text)
            if match:
                start=match.end(); end=start; depth=1
                while end<len(text) and depth:
                    if text[end]=='{': depth+=1
                    elif text[end]=='}': depth-=1
                    end+=1
                require(depth==0,'UNTERMINATED_SCRIPT_SECTIONS')
                body=text[start:end-1]
                number=r'(?:0x[0-9a-fA-F]+|[0-9]+)'
                assignment=r'\.\s*=\s*'+number+r'\s*;'
                selector=r'\*\s*\(\s*\.[A-Za-z0-9_.*]+\s*\)'
                section=r'\.[A-Za-z0-9_.]+\s*:\s*\{\s*(?:'+selector+r'\s*)+\}'
                require(re.fullmatch(r'\s*(?:(?:'+assignment+'|'+section+r')\s*)*',body) is not None,
                        'UNQUALIFIED_SECTION_LAYOUT')
                text=text[:match.start()]+text[end:]
            lexer = shlex.shlex(text, posix=True, punctuation_chars="(),;")
            lexer.whitespace_split = True; lexer.commenters = ""
            tokens = list(lexer)
            # shlex groups adjacent punctuation; split it deterministically.
            tokens = [p for t in tokens for p in (list(t) if t and all(c in "(),;" for c in t) else [t])]
            i = 0
            while i < len(tokens):
                directive = tokens[i]; i += 1
                if directive == ";": continue
                require(directive in ("INCLUDE", "SEARCH_DIR", "GROUP", "INPUT", "OUTPUT_FORMAT", "OUTPUT_ARCH", "ENTRY", "EXTERN"),
                        "UNQUALIFIED_LINKER_SCRIPT_SEMANTICS: " + directive)
                if directive == "INCLUDE":
                    require(i < len(tokens), "MISSING_INCLUDE")
                    included = self.resolve(tokens[i], str(PurePosixPath(path).parent)); i += 1
                    self.script(included)
                    continue
                require(i < len(tokens) and tokens[i] == "(", "MISSING_SCRIPT_PAREN")
                i += 1; args = []; depth = 1
                while i < len(tokens) and depth:
                    token = tokens[i]; i += 1
                    if token == "(": depth += 1
                    elif token == ")": depth -= 1
                    if depth: args.append(token)
                require(depth == 0, "UNTERMINATED_SCRIPT_DIRECTIVE")
                args = [a for a in args if a not in (",", ";")]
                if directive == "SEARCH_DIR":
                    require(len(args) == 1, "SEARCH_DIR_ARITY")
                    root = normalized(args[0], self.cwd)
                    require(root == "/snapshot" or root.startswith("/snapshot/"), "FOREIGN_SEARCH_DIR")
                    self.search.append(root)
                elif directive in ("INPUT", "GROUP"):
                    require(bool(args), "EMPTY_SCRIPT_INPUT")
                    for token in args:
                        require(token not in ("(", ")", "AS_NEEDED"), "UNSUPPORTED_SCRIPT_GROUP_SEMANTICS")
                        resolved = self.resolve(token, str(PurePosixPath(path).parent))
                        if resolved.endswith(".ld"): self.script(resolved)
                else:
                    require(bool(args) and all(re.fullmatch(r"[A-Za-z0-9_.*:+-]+", a) for a in args), "INVALID_SCRIPT_SYMBOL_OR_FORMAT")
        finally: self.active_scripts.remove(path)


def verify_pair_closures(a, b, approved_tesla):
    fields = {"schema", "common", "tesla", "actions"}
    for value in (a, b):
        require(type(value) is dict and set(value) == fields and value["schema"] == "warp4-action-closure-v1", "ACTION_CLOSURE_SCHEMA")
        require(type(value["actions"]) is list, "ACTION_LIST_REQUIRED")
        for kind in ("common", "tesla"):
            require(type(value[kind]) is dict and all(type(p) is str and p.startswith("/snapshot/") and
                    ".." not in PurePosixPath(p).parts and hexpin(h) for p, h in value[kind].items()), "ACTION_INPUT_INVENTORY")
    validate_actions(a)
    validate_actions(b)
    require(a["common"] == b["common"] and bool(a["common"]), "AB_COMMON_CLOSURE_DIFFERS")
    require(not a["tesla"] and b["tesla"] == approved_tesla and not (set(b["common"]) & set(b["tesla"])), "AB_TESLA_DELTA_DIFFERS")
    return True


def archive_members(data):
    """Resolve ordinary GNU/BSD ar member names; reject thin/external members."""
    import hashlib
    require(data.startswith(b"!<arch>\n"), "ORDINARY_ARCHIVE_REQUIRED")
    pos, table, result = 8, None, {}
    while pos < len(data):
        require(pos + 60 <= len(data) and data[pos+58:pos+60] == b"`\n", "AR_HEADER")
        name = data[pos:pos+16].decode("ascii").strip()
        length = data[pos+48:pos+58].strip()
        require(length.isdigit(), "AR_SIZE")
        size = int(length); pos += 60
        require(pos+size <= len(data), "AR_TRUNCATED")
        member = data[pos:pos+size]; pos += size
        if size % 2:
            require(data[pos:pos+1] == b"\n", "AR_PADDING")
            pos += 1
        if name == "//":
            require(table is None, "AR_DUPLICATE_NAME_TABLE")
            table = member; continue
        if name in ("/", "/SYM64/", "__.SYMDEF", "__.SYMDEF SORTED"): continue
        if name.startswith("#1/"):
            require(name[3:].isdigit() and 0 < int(name[3:]) <= len(member), "AR_BSD_NAME")
            n = int(name[3:]); name = member[:n].rstrip(b"\0").decode(); member = member[n:]
        elif name.startswith("/"):
            require(name[1:].isdigit() and table is not None, "AR_NAME_REFERENCE")
            offset = int(name[1:])
            require(offset < len(table) and (offset == 0 or table[offset-1:offset] == b"\n"), "AR_NAME_OFFSET")
            end = table.find(b"/\n", offset)
            require(end >= offset, "AR_NAME_UNTERMINATED")
            name = table[offset:end].decode()
        else: name = name.removesuffix("/")
        require(name and name not in result and "/" not in name and "\\" not in name and name not in (".", ".."), "AR_UNSAFE_OR_DUPLICATE_MEMBER")
        result[name] = hashlib.sha256(member).hexdigest()
    require(pos == len(data) and bool(result), "AR_EMPTY_OR_TRAILING")
    return result


def validate_actions(closure):
    """Validate ordered hash-bound action evidence, not its capture provenance.

    A producer/tracer must still prove these records describe all real opens and
    execs. Handwritten records are parser fixtures, never integration evidence.
    """
    require(bool(closure["actions"]), "EMPTY_ACTUAL_ACTION_GRAPH")
    immutable = closure["common"] | closure["tesla"]
    generated, consumed = {}, {}
    for action in closure["actions"]:
        require(type(action) is dict and set(action) == {"tool", "kind", "argv", "cwd", "inputs", "outputs"}, "ACTION_SCHEMA")
        require(action["kind"] in ("compiler", "assembler", "linker", "archiver", "objcopy", "node", "npm", "python"), "UNKNOWN_ACTION_KIND")
        require(type(action["argv"]) is list and bool(action["argv"]) and all(type(a) is str for a in action["argv"]) and
                action["argv"][0] == action["tool"] and action["tool"] in immutable, "FOREIGN_ACTION_TOOL")
        cwd = normalized(action["cwd"], "/")
        require(cwd == "/output" or cwd.startswith("/output/") or cwd == "/snapshot/project", "ACTION_WORKDIR_ESCAPE")
        require(type(action["inputs"]) is dict and bool(action["inputs"]) and action["tool"] in action["inputs"] and type(action["outputs"]) is dict, "ACTION_INPUTS_REQUIRED")
        for path, expected in action["inputs"].items():
            require(hexpin(expected) and normalized(path, cwd) == path, "ACTION_INPUT_SCHEMA")
            if path in immutable:
                require(expected == immutable[path], "ACTION_IMMUTABLE_HASH_MISMATCH")
                consumed[path] = expected
            else:
                require(path in generated and expected == generated[path], "UNRECORDED_GENERATED_INPUT")
        for path, expected in action["outputs"].items():
            require(type(path) is str and path.startswith("/output/") and normalized(path, cwd) == path and hexpin(expected), "ACTION_OUTPUT_ESCAPE")
            generated[path] = expected
    require(consumed == immutable, "UNCONSUMED_OR_INCOMPLETE_IMMUTABLE_GRAPH")
    return True
