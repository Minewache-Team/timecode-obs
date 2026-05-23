<?php
/**
 * MW-Aufnahme System - Latest Plugin Version Resolver
 *
 * Liefert die zuletzt veroeffentlichte Plugin-Version. Holt sie automatisch
 * von der GitHub Releases API und cached fuer 1 Stunde, damit das Dashboard
 * bei jedem Tag-Push automatisch die neue Version als "current" erkennt
 * ohne dass jemand index.php anfassen muss.
 *
 * Fallback-Kette:
 *   1. Frischer GitHub-Fetch (max 1 x pro Stunde)
 *   2. Letzter gecachter Wert (auch wenn abgelaufen)
 *   3. Konstante MW_FALLBACK_LATEST_VERSION aus config.php (optional)
 *   4. Hardcoded "0.6.1" als letztes Bollwerk
 *
 * Cache liegt in /website/.latest_version_cache (gitignored). Eine
 * stream_context-Timeout von 5 s schuetzt die Dashboard-Ladezeit falls
 * GitHub langsam antwortet.
 */

define('MW_LATEST_VERSION_CACHE_TTL', 3600);
define('MW_LATEST_VERSION_HARDCODED_FALLBACK', '0.6.2');

function _mw_latest_version_cache_path(): string
{
    return __DIR__ . '/../.latest_version_cache';
}

/* Override file written by the director via the dashboard UI
 * (dashboard-api.php?action=set_latest_version). Higher priority than
 * the GitHub fetch, lower than the config.php constant. Gives the
 * director one-click control without touching PHP/FTP. */
function _mw_latest_version_override_path(): string
{
    return __DIR__ . '/../.latest_version_override';
}

function _mw_latest_version_read_override(): ?string
{
    $path = _mw_latest_version_override_path();
    if (!file_exists($path)) {
        return null;
    }
    $value = trim((string) @file_get_contents($path));
    if ($value === '' || !preg_match('/^[\w.+-]{1,20}$/', $value)) {
        return null;
    }
    return $value;
}

function _mw_latest_version_http_get(string $url): ?string
{
    /* cURL bevorzugt — universell verfuegbar und arbeitet auch wenn
     * allow_url_fopen=off ist (haeufig auf Shared-Hosting). file_get_contents
     * mit stream_context_create ist Fallback fuer den seltenen Fall dass
     * cURL nicht installiert ist. */
    if (function_exists('curl_init')) {
        $ch = curl_init($url);
        curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
        curl_setopt($ch, CURLOPT_FOLLOWLOCATION, true);
        curl_setopt($ch, CURLOPT_TIMEOUT, 5);
        curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 3);
        curl_setopt($ch, CURLOPT_USERAGENT, 'mw-aufnahme-dashboard');
        curl_setopt($ch, CURLOPT_HTTPHEADER, ['Accept: application/vnd.github+json']);
        curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, true);
        curl_setopt($ch, CURLOPT_SSL_VERIFYHOST, 2);
        $body = curl_exec($ch);
        $code = (int) curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);
        if (is_string($body) && $code >= 200 && $code < 300) {
            return $body;
        }
        /* cURL hat geantwortet aber nicht erfolgreich — fall through zu
         * stream_context als Notnagel. */
    }

    $ctx = stream_context_create([
        'http' => [
            'method' => 'GET',
            'header' => "User-Agent: mw-aufnahme-dashboard\r\nAccept: application/vnd.github+json\r\n",
            'timeout' => 5,
            'ignore_errors' => true,
        ],
        'ssl' => [
            'verify_peer' => true,
            'verify_peer_name' => true,
        ],
    ]);
    $body = @file_get_contents($url, false, $ctx);
    if ($body === false) {
        return null;
    }
    return $body;
}

function _mw_latest_version_fetch_from_github(): ?string
{
    /* Wichtig: wir wollen explizit den neuesten Minewache-Branch-Release,
     * nicht den ggf. parallelen master-Release. Alle Minewache-Releases
     * tragen "Minewache Specific Version" im Titel — wir holen daher die
     * Liste der letzten 20 Releases und nehmen den ersten passenden. Faellt
     * der Filter ins Leere (z.B. weil GitHub-Titel sich aendert), greift
     * die Stale-Cache-Fallback-Kette eine Ebene drueber. */
    $url = 'https://api.github.com/repos/Minewache-Team/timecode-obs/releases?per_page=20';
    $body = _mw_latest_version_http_get($url);
    if ($body === null) {
        return null;
    }

    $releases = json_decode($body, true);
    if (!is_array($releases)) {
        return null;
    }

    foreach ($releases as $rel) {
        if (!is_array($rel)) continue;
        if (!empty($rel['draft']) || !empty($rel['prerelease'])) continue;

        /* Branch-Filter via Title (alle Minewache-Releases haben das im
         * Namen, ergaenzt von CI). Backup: target_commitish (manchmal die
         * Branch, manchmal die Commit-SHA — daher nur als Hinweis). */
        $name = isset($rel['name']) && is_string($rel['name']) ? $rel['name'] : '';
        $is_minewache = stripos($name, 'Minewache') !== false;
        if (!$is_minewache) continue;

        if (!isset($rel['tag_name']) || !is_string($rel['tag_name'])) continue;
        $tag = trim($rel['tag_name']);

        /* Plausibilitaetscheck: gleiche Regex wie api.php fuer
         * plugin_version-Validierung (^[\w.+-]{1,20}$). Schuetzt vor
         * merkwuerdigen Tags und HTML-Injection-Versuchen. */
        if (!preg_match('/^[\w.+-]{1,20}$/', $tag)) continue;

        return $tag;
    }

    return null;
}

function get_latest_plugin_version(): string
{
    /* 1. Admin override via config.php constant — for ops who want it in
     *    version control or as a deploy-time pin. */
    if (defined('MW_FALLBACK_LATEST_VERSION')) {
        return MW_FALLBACK_LATEST_VERSION;
    }

    /* 2. Dashboard-set override file — director clicks Save on the input
     *    field at the top of the dashboard, writes here, wins over
     *    GitHub-fetch and cache. */
    $override = _mw_latest_version_read_override();
    if ($override !== null) {
        return $override;
    }

    $cache_path = _mw_latest_version_cache_path();

    /* 2. Fresh cache hit (< 1 h) */
    if (file_exists($cache_path)) {
        $age = time() - filemtime($cache_path);
        if ($age < MW_LATEST_VERSION_CACHE_TTL) {
            $cached = trim((string) @file_get_contents($cache_path));
            if ($cached !== '') {
                return $cached;
            }
        }
    }

    /* 3. Try GitHub */
    $fetched = _mw_latest_version_fetch_from_github();
    if ($fetched !== null) {
        @file_put_contents($cache_path, $fetched);
        return $fetched;
    }

    /* 4. Stale cache rather than nothing */
    if (file_exists($cache_path)) {
        $stale = trim((string) @file_get_contents($cache_path));
        if ($stale !== '') {
            /* Touch the cache so we don't hammer GitHub on every page load
             * if it's currently down — wait the full TTL before retrying. */
            @touch($cache_path);
            return $stale;
        }
    }

    /* 5. Hardcoded last-resort */
    return MW_LATEST_VERSION_HARDCODED_FALLBACK;
}
