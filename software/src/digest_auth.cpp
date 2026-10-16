/*
    This is a stripped down version supporting only digest authentication.
    Taken from https://github.com/me-no-dev/ESPAsyncWebServer
*/
/*
  Asynchronous WebServer library for Espressif MCUs

  Copyright (c) 2016 Hristo Gochkov. All rights reserved.
  This file is part of the esp8266 core for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include "digest_auth.h"

#include <mbedtls/md5.h>

#include "event_log_prefix.h"
#include "main_dependencies.h"
#include "build.h"
#include "cool_string.h"
#include <initializer_list>

#define MD5_LEN 32

static bool getMD5(uint8_t * data, uint16_t len, char * output){//33 bytes or more
    uint8_t i;
    uint8_t _buf[16];
    memset(_buf, 0x00, 16);

    if (mbedtls_md5(data, len, _buf))
        return false;

    for(i = 0; i < 16; i++) {
        sprintf(output + (i * 2), "%02x", _buf[i]);
    }
    return true;
}

static String genRandomString(){
    uint8_t data[16] = {0};
    uint32_t t = now_us().as<uint32_t>();
    memcpy(data, &t, sizeof(t));
    t = build_timestamp();
    memcpy(data + 4, &t, sizeof(t));
    uint32_t r = esp_random();
    memcpy(data + 8, &r, sizeof(r));
    r = esp_random();
    memcpy(data + 12, &r, sizeof(r));

    char buf[33] = {0};
    if (!getMD5(data, 16, buf)) { return ""; }

    return String(buf);
}

static bool md5Hex(const String &value)
{
    if (value.length() != MD5_LEN) { return false; }
    for (unsigned i = 0; i < MD5_LEN; ++i) {
        const char c = value.c_str()[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) { return false; }
    }
    return true;
}

// Hash transcript parts directly: unchecked Arduino String concatenations can
// silently truncate on allocation failure. Only the output allocates, and all
// callers validate its exact size before using it as a hash or credential.
static String stringMD5(std::initializer_list<const char *> parts){
    CoolString out;
    if (!out.reserve(MD5_LEN + 1))
        return "";

    mbedtls_md5_context context;
    mbedtls_md5_init(&context);
    bool ok = mbedtls_md5_starts(&context) == 0;
    for (const char *part : parts) {
        if (!ok || part == nullptr) { ok = false; break; }
        ok = mbedtls_md5_update(&context, reinterpret_cast<const uint8_t *>(part), strlen(part)) == 0;
    }
    uint8_t digest[16] = {};
    if (ok) { ok = mbedtls_md5_finish(&context, digest) == 0; }
    mbedtls_md5_free(&context);
    if (!ok) { return ""; }
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < sizeof(digest); ++i) {
        out.begin()[2 * i] = hex[digest[i] >> 4];
        out.begin()[2 * i + 1] = hex[digest[i] & 15];
    }
    out.begin()[MD5_LEN] = 0;
    out.setLength(MD5_LEN);
    return out;
}

String requestDigestAuthentication(const char * realm){
    String header = "realm=\"";
    if(realm == NULL) {
        header.concat(DEFAULT_REALM);
    } else {
        header.concat(realm);
    }
    header.concat( "\", qop=\"auth\", nonce=\"");
    header.concat(genRandomString());
    header.concat("\", opaque=\"");
    header.concat(genRandomString());
    header.concat("\"");
    return header;
}

AuthFields parseDigestAuth(const char *header)
{
    AuthFields result;
    result.success = false;

    if (header == nullptr) {
        logger.printfln("AUTH FAIL: missing required fields");
        return result;
    }

    String myHeader = String(header);
    int nextBreak = myHeader.indexOf(",");
    if (nextBreak < 0) {
        logger.printfln("AUTH FAIL: no variables");
        return result;
    }

    myHeader += ", ";
    do {
        String avLine = myHeader.substring(0, nextBreak);
        avLine.trim();
        myHeader = myHeader.substring(nextBreak + 1);
        nextBreak = myHeader.indexOf(",");

        int eqSign = avLine.indexOf("=");
        if (eqSign < 0) {
            logger.printfln("AUTH FAIL: no = sign");
            return result;
        }

        String varName = avLine.substring(0, eqSign);
        avLine = avLine.substring(eqSign + 1);
        if (avLine.startsWith("\"")) {
            avLine = avLine.substring(1, avLine.length() - 1);
        }

        if (varName.equals("username")) {
            result.username = avLine;
        } else if (varName.equals("realm")) {
            result.realm = avLine;
        } else if (varName.equals("nonce")) {
            result.nonce = avLine;
        } else if (varName.equals("opaque")) {
            result.opaque = avLine;
        } else if (varName.equals("uri")) {
            result.uri = avLine;
        } else if (varName.equals("response")) {
            result.response = avLine;
        } else if (varName.equals("qop")) {
            result.qop = avLine;
        } else if (varName.equals("nc")) {
            result.nc = avLine;
        } else if (varName.equals("cnonce")) {
            result.cnonce = avLine;
        }
    } while (nextBreak > 0);

    // Missing/empty/malformed responses must not equal an empty hash on OOM.
    result.success = md5Hex(result.response);
    return result;
}

bool checkDigestAuthentication(const AuthFields &fields, const char * method, const char * username, const char * password, const char * realm, bool passwordIsHash, const char * nonce, const char * opaque, const char * uri){
    if (!fields.success || !md5Hex(fields.response) || username == NULL || password == NULL || method == NULL) {
        logger.printfln("AUTH FAIL: missing required fields");
        return false;
    }

    if (!fields.username.equals(username)) {
        logger.printfln("AUTH FAIL: username");
        return false;
    }

    if (realm != NULL && !fields.realm.equals(realm)) {
        logger.printfln("AUTH FAIL: realm");
        return false;
    } else if (realm == NULL && !fields.realm.equals(DEFAULT_REALM) && !fields.realm.equals("asyncesp")) {
        logger.printfln("AUTH FAIL: realm");
        return false;
    }

    if (nonce != NULL && !fields.nonce.equals(nonce)) {
        logger.printfln("AUTH FAIL: nonce");
        return false;
    }

    if (opaque != NULL && !fields.opaque.equals(opaque)) {
        logger.printfln("AUTH FAIL: opaque");
        return false;
    }
    if (uri != NULL && !fields.uri.equals(uri)) {
        logger.printfln("AUTH FAIL: uri");
        return false;
    }

    String ha1 = (passwordIsHash) ? String(password) : stringMD5({fields.username.c_str(), ":", fields.realm.c_str(), ":", password});
    if (!md5Hex(ha1)) {
        logger.printfln("AUTH FAIL: out of memory");
        return false;
    }

    String ha2 = stringMD5({method, ":", fields.uri.c_str()});
    if (!md5Hex(ha2)) {
        logger.printfln("AUTH FAIL: out of memory");
        return false;
    }

    const String response = stringMD5({ha1.c_str(), ":", fields.nonce.c_str(), ":", fields.nc.c_str(), ":", fields.cnonce.c_str(), ":", fields.qop.c_str(), ":", ha2.c_str()});

    if (md5Hex(response) && fields.response.equals(response)) {
        return true;
    }

    logger.printfln("AUTH FAIL: password");
    return false;
}

String generateDigestHash(const char * username, const char * password, const char * realm){
    if(username == NULL || password == NULL || realm == NULL){
        return "";
    }

    return stringMD5({username, ":", realm, ":", password});
}
