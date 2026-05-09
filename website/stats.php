<?php
/**
 * MW-Aufnahme System - Folgen-Statistik
 *
 * Zeigt pro Folge alle beteiligten Kameraleute (ueber alle Szenen hinweg).
 */

require_once __DIR__ . '/includes/db.php';
require_dashboard_auth();
?>
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MW Aufnahme - Folgen-Statistik</title>
    <link rel="stylesheet" href="assets/style.css">
</head>
<body>
    <div class="container">
        <header>
            <h1>Folgen-Statistik</h1>
            <nav>
                <a href="index.php">Dashboard</a>
                <a href="scenes.php" class="active">Szenen</a>
                <a href="datenschutz.php">Datenschutz</a>
                <a href="login.php?logout=1" style="color:#f44336;">Logout</a>
            </nav>
        </header>

        <div class="sub-nav">
            <a href="scenes.php">Szenen-Archiv</a>
            <a href="stats.php" class="active">Folgen-Statistik</a>
        </div>

        <div id="stats-content">
            <p class="empty-state">Lade Statistik...</p>
        </div>
    </div>

    <script>
    (function () {
        fetch('dashboard-api.php?action=episode_stats')
            .then(function (res) { return res.json(); })
            .then(function (data) {
                if (!data.ok || !data.episodes || data.episodes.length === 0) {
                    document.getElementById('stats-content').innerHTML =
                        '<p class="empty-state">Noch keine Szenen gespeichert.</p>';
                    return;
                }
                renderStats(data.episodes);
            })
            .catch(function () {
                document.getElementById('stats-content').innerHTML =
                    '<p class="empty-state" style="color:#f44336;">Fehler beim Laden der Statistik.</p>';
            });

        function renderStats(episodes) {
            var html = '<table class="stats-table">'
                + '<thead><tr>'
                + '<th>Staffel</th>'
                + '<th>Folge</th>'
                + '<th>Szenen</th>'
                + '<th>Zeitraum</th>'
                + '<th>Beteiligte</th>'
                + '</tr></thead><tbody>';

            episodes.forEach(function (ep) {
                var timeRange = '-';
                if (ep.first_start) {
                    var start = formatDateTime(ep.first_start);
                    var stop = ep.last_stop ? formatDateTime(ep.last_stop) : 'laeuft';
                    timeRange = start + ' &ndash; ' + stop;
                }

                var chips = '';
                if (ep.participants && ep.participants.length > 0) {
                    chips = '<div class="participant-chips">';
                    ep.participants.forEach(function (p) {
                        chips += '<span class="participant-chip">'
                            + '<span class="cam-id">' + escapeHtml(p.camera_id) + '</span> '
                            + escapeHtml(p.user_name)
                            + '</span>';
                    });
                    chips += '</div>';
                } else {
                    chips = '<span style="color:#666;">-</span>';
                }

                html += '<tr>'
                    + '<td>S' + pad(ep.season) + '</td>'
                    + '<td>E' + pad(ep.episode) + '</td>'
                    + '<td>' + ep.scene_count + '</td>'
                    + '<td>' + timeRange + '</td>'
                    + '<td>' + chips + '</td>'
                    + '</tr>';
            });

            html += '</tbody></table>';
            document.getElementById('stats-content').innerHTML = html;
        }

        function pad(n) {
            return n < 10 ? '0' + n : '' + n;
        }

        function formatDateTime(str) {
            if (!str) return '-';
            try {
                var d = new Date(str);
                return d.toLocaleDateString('de-DE', { day: '2-digit', month: '2-digit' })
                    + ' ' + d.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' });
            } catch (e) {
                return str;
            }
        }

        function escapeHtml(str) {
            var div = document.createElement('div');
            div.appendChild(document.createTextNode(str || ''));
            return div.innerHTML;
        }
    })();
    </script>
</body>
</html>
