// Formats the WARP-side receive time of the last authenticated Tesla SoC.
// unixSeconds comes from tesla_vehicle/state last_update_unix, which the
// firmware sets from its synchronized clock at reception (0 = WARP clock not
// synchronized). Uptime is never presented as a time of day.

export interface SocTimeLabels {
    unsynced: string;
    today: string;
}

function formatter(timeZone: string | undefined, options: Intl.DateTimeFormatOptions) {
    if (timeZone) {
        try { return new Intl.DateTimeFormat(undefined, {...options, timeZone}); }
        catch (e) { /* Unknown zone name: fall back to the browser zone below. */ }
    }
    return new Intl.DateTimeFormat(undefined, options);
}

// timeZone: the WARP's configured IANA zone (ntp/config timezone), so the
// displayed wall-clock time matches the charger, not the viewing device.
export function formatSocReceiveTime(unixSeconds: number, timeZone: string | undefined, nowMs: number, labels: SocTimeLabels): string {
    if (!Number.isFinite(unixSeconds) || unixSeconds <= 0) return labels.unsynced;
    const at = unixSeconds * 1000;
    const day = formatter(timeZone, {year: 'numeric', month: '2-digit', day: '2-digit'});
    const time = formatter(timeZone, {hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false}).format(at);
    return day.format(at) === day.format(nowMs) ? labels.today + ', ' + time : day.format(at) + ', ' + time;
}
