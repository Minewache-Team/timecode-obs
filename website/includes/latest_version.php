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

function _mw_latest_version_fetch_from_github(): ?string
{
    /* Wichtig: wir wollen explizit den neuesten Minewache-Branch-Release,
     * nicht den ggf. parallelen master-Release. Alle Minewache-Releases
     * tragen "Minewache Specific Version" im Titel — wir holen daher die
     * Liste der letzten 20 Releases und nehmen den ersten passenden. Faellt
     * der Filter ins Leere (z.B. weil GitHub-Titel sich aendert), greift
     * die Stale-Cache-Fallback-Kette eine Ebene drueber. */
    $url = 'https://api.github.com/repos/Minewache-Team/timecode-obs/releases?per_page=20';
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

    /* @ unterdrueckt Warnung wenn z.B. allow_url_fopen=off oder Netz tot;
     * wir fangen das ueber den false-Rueckgabewert ab. */
    $body = @file_get_contents($url, false, $ctx);
    if ($body === false) {
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
    $cache_path = _mw_latest_version_cache_path();

    /* 1. Fresh cache hit */
    if (file_exists($cache_path)) {
        $age = time() - filemtime($cache_path);
        if ($age < MW_LATEST_VERSION_CACHE_TTL) {
            $cached = trim((string) @file_get_contents($cache_path));
            if ($cached !== '') {
                return $cached;
            }
        }
    }

    /* 2. Try GitHub */
    $fetched = _mw_latest_version_fetch_from_github();
    if ($fetched !== null) {
        @file_put_contents($cache_path, $fetched);
        return $fetched;
    }

    /* 3. Stale cache rather than nothing */
    if (file_exists($cache_path)) {
        $stale = trim((string) @file_get_contents($cache_path));
        if ($stale !== '') {
            /* Touch the cache so we don't hammer GitHub on every page load
             * if it's currently down — wait the full TTL before retrying. */
            @touch($cache_path);
            return $stale;
        }
    }

    /* 4. Config override */
    if (defined('MW_FALLBACK_LATEST_VERSION')) {
        return MW_FALLBACK_LATEST_VERSION;
    }

    /* 5. Hardcoded last-resort */
    return MW_LATEST_VERSION_HARDCODED_FALLBACK;
}
