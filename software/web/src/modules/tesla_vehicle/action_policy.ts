export function teslaActions(enable: boolean, status: {
    running?: boolean; stopping?: boolean; pairing_pending?: boolean;
    restart_required?: boolean; paired?: boolean; key_present?: boolean;
    key_presence?: 'unknown' | 'present' | 'absent';
}, dirty: boolean) {
    const stopping = !!status?.stopping;
    return {
        transition: stopping,
        pairDisabled: stopping || !enable || !status?.running || status?.pairing_pending || status?.restart_required || dirty,
        pollDisabled: stopping || !status?.paired || !status?.running || status?.restart_required || dirty,
        deleteDisabled: stopping || !!status?.restart_required || status?.key_presence === 'absent'
    };
}
