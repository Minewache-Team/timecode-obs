<?php
/**
 * MW-Aufnahme System - Szenen-Archiv
 *
 * Zeigt alle gespeicherten Szenen in einer Tabelle an.
 * Klick auf eine Szene zeigt die beteiligten Kameraleute.
 */

require_once __DIR__ . '/includes/db.php';
require_dashboard_auth();

$scenes = [];
$session_map = [];
$db_error = '';
try {
    $db = get_db();
    $stmt = $db->query(
        "SELECT * FROM scenes ORDER BY season ASC, episode ASC, created_at ASC"
    );
    $scenes = $stmt->fetchAll();

    /* Alle referenzierten Session-IDs sammeln */
    $all_ids = [];
    foreach ($scenes as $scene) {
        if (!empty($scene['session_ids'])) {
            foreach (explode(',', $scene['session_ids']) as $id) {
                $id = (int) trim($id);
                if ($id > 0) $all_ids[$id] = true;
            }
        }
    }

    /* Sessions laden */
    if (!empty($all_ids)) {
        $ids = array_keys($all_ids);
        $placeholders = implode(',', array_fill(0, count($ids), '?'));
        $stmt = $db->prepare(
            "SELECT id, user_name, camera_id, started_at, stopped_at
             FROM sessions WHERE id IN ($placeholders)"
        );
        $stmt->execute($ids);
        foreach ($stmt->fetchAll() as $s) {
            $session_map[(int) $s['id']] = $s;
        }
    }
} catch (PDOException $e) {
    $db_error = $e->getMessage();
}
?>
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MW Aufnahme - Szenen-Archiv</title>
    <link rel="stylesheet" href="assets/style.css">
    <style>
        .scene-table tbody tr.scene-row {
            cursor: pointer;
        }
        .scene-table tbody tr.scene-row:hover {
            background: #1a2744;
        }
        .scene-detail {
            display: none;
        }
        .scene-detail.open {
            display: table-row;
        }
        .scene-detail td {
            padding: 0;
            border-bottom: 1px solid #222;
        }
        .detail-content {
            padding: 12px 20px 16px;
            background: #0f1e3a;
        }
        .detail-content h4 {
            color: #4fc3f7;
            font-size: 0.85rem;
            text-transform: uppercase;
            margin-bottom: 10px;
        }
        .camera-list {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
        }
        .camera-chip {
            display: inline-flex;
            align-items: center;
            gap: 8px;
            background: #16213e;
            border: 1px solid #333;
            border-radius: 6px;
            padding: 8px 14px;
            font-size: 0.85rem;
        }
        .camera-chip .cam-letter {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            width: 26px;
            height: 26px;
            border-radius: 50%;
            background: #4fc3f7;
            color: #1a1a2e;
            font-weight: bold;
            font-size: 0.8rem;
        }
        .camera-chip .cam-name {
            color: #e0e0e0;
            font-weight: 600;
        }
        .camera-chip .cam-time {
            color: #888;
            font-size: 0.8rem;
        }
        .no-sessions {
            color: #666;
            font-style: italic;
            font-size: 0.85rem;
        }
        .expand-icon {
            display: inline-block;
            transition: transform 0.2s;
            color: #666;
            margin-right: 6px;
        }
        .scene-row.open .expand-icon {
            transform: rotate(90deg);
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>Szenen-Archiv</h1>
            <nav>
                <a href="index.php">Dashboard</a>
                <a href="scenes.php" class="active">Szenen</a>
                <a href="datenschutz.php">Datenschutz</a>
                <a href="login.php?logout=1" style="color:#f44336;">Logout</a>
            </nav>
        </header>

        <div class="sub-nav">
            <a href="scenes.php" class="active">Szenen-Archiv</a>
            <a href="stats.php">Folgen-Statistik</a>
        </div>

        <?php if ($db_error): ?>
            <div style="background:#3a1010;border:1px solid #f44336;padding:15px;border-radius:8px;margin-bottom:20px;">
                <p style="color:#f44336;font-weight:bold;">Datenbankfehler</p>
                <p style="color:#ccc;"><?= htmlspecialchars($db_error) ?></p>
                <p style="color:#888;margin-top:10px;">Hast du <a href="install.php" style="color:#4fc3f7;">install.php</a> schon aufgerufen?</p>
            </div>
        <?php elseif (empty($scenes)): ?>
            <p class="empty-state">Noch keine Szenen gespeichert.</p>
        <?php else: ?>
            <table class="scene-table">
                <thead>
                    <tr>
                        <th>Staffel</th>
                        <th>Folge</th>
                        <th>Szene</th>
                        <th>Versuch</th>
                        <th>Kameras</th>
                        <th>Start</th>
                        <th>Ende</th>
                        <th>Erstellt</th>
                        <th></th>
                    </tr>
                </thead>
                <tbody>
                    <?php foreach ($scenes as $i => $scene):
                        /* Beteiligte Sessions ermitteln */
                        $participants = [];
                        if (!empty($scene['session_ids'])) {
                            foreach (explode(',', $scene['session_ids']) as $sid) {
                                $sid = (int) trim($sid);
                                if (isset($session_map[$sid])) {
                                    $participants[] = $session_map[$sid];
                                }
                            }
                        }
                        $cam_count = count($participants);
                    ?>
                        <tr class="scene-row" data-target="detail-<?= $i ?>" data-scene-id="<?= (int)$scene['id'] ?>" onclick="toggleDetail(this)">
                            <td><span class="expand-icon">&#9654;</span>S<?= str_pad($scene['season'], 2, '0', STR_PAD_LEFT) ?></td>
                            <td>E<?= str_pad($scene['episode'], 2, '0', STR_PAD_LEFT) ?></td>
                            <td><?= $scene['scene_name'] !== '' ? htmlspecialchars($scene['scene_name']) : '<span style="color:#666;">-</span>' ?></td>
                            <td><?= (int)($scene['take'] ?? 1) ?></td>
                            <td><?= $cam_count > 0 ? $cam_count . ' Kamera' . ($cam_count > 1 ? 's' : '') : '-' ?></td>
                            <td><?= $scene['started_at'] ? date('H:i:s', strtotime($scene['started_at'])) : '-' ?></td>
                            <td><?= $scene['stopped_at'] ? date('H:i:s', strtotime($scene['stopped_at'])) : '-' ?></td>
                            <td><?= date('d.m.Y H:i', strtotime($scene['created_at'])) ?></td>
                            <td><button class="scene-delete-btn" onclick="event.stopPropagation();deleteScene(<?= (int)$scene['id'] ?>,'<?= addslashes(htmlspecialchars($scene['scene_name'])) ?>')">Loeschen</button></td>
                        </tr>
                        <tr class="scene-detail" id="detail-<?= $i ?>">
                            <td colspan="9">
                                <div class="detail-content">
                                    <h4>Beteiligte Kameraleute</h4>
                                    <?php if (empty($participants)): ?>
                                        <p class="no-sessions">Keine Session-Daten vorhanden.</p>
                                    <?php else: ?>
                                        <div class="camera-list">
                                            <?php foreach ($participants as $p): ?>
                                                <div class="camera-chip">
                                                    <span class="cam-letter"><?= htmlspecialchars($p['camera_id']) ?></span>
                                                    <span>
                                                        <span class="cam-name"><?= htmlspecialchars($p['user_name']) ?></span><br>
                                                        <span class="cam-time">
                                                            <?= $p['started_at'] ? date('H:i:s', strtotime($p['started_at'])) : '?' ?>
                                                            &ndash;
                                                            <?= $p['stopped_at'] ? date('H:i:s', strtotime($p['stopped_at'])) : 'läuft' ?>
                                                        </span>
                                                    </span>
                                                </div>
                                            <?php endforeach; ?>
                                        </div>
                                    <?php endif; ?>
                                    <?php if (!empty($scene['notes'])): ?>
                                        <p style="margin-top:12px;color:#aaa;font-size:0.85rem;">
                                            <strong>Notizen:</strong> <?= htmlspecialchars($scene['notes']) ?>
                                        </p>
                                    <?php endif; ?>
                                </div>
                            </td>
                        </tr>
                    <?php endforeach; ?>
                </tbody>
            </table>
        <?php endif; ?>
    </div>

    <!-- Toast Benachrichtigung -->
    <div class="toast" id="toast"></div>

    <script>
    function toggleDetail(row) {
        const targetId = row.dataset.target;
        const detail = document.getElementById(targetId);
        if (!detail) return;

        row.classList.toggle('open');
        detail.classList.toggle('open');
    }

    function deleteScene(sceneId, sceneName) {
        if (!confirm('Szene "' + sceneName + '" wirklich loeschen?\n\nDiese Aktion kann nicht rueckgaengig gemacht werden.')) {
            return;
        }

        fetch('dashboard-api.php?action=delete_scene', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ scene_id: sceneId }),
        })
            .then(function (res) { return res.json(); })
            .then(function (data) {
                if (data.ok) {
                    showToast(data.message || 'Szene geloescht');
                    var row = document.querySelector('[data-scene-id="' + sceneId + '"]');
                    if (row) {
                        var detail = row.nextElementSibling;
                        if (detail && detail.classList.contains('scene-detail')) {
                            detail.remove();
                        }
                        row.remove();
                    }
                } else {
                    showToast(data.error || 'Fehler', true);
                }
            })
            .catch(function () {
                showToast('Netzwerkfehler', true);
            });
    }

    var toast = document.getElementById('toast');
    function showToast(message, isError) {
        if (!toast) return;
        toast.textContent = message;
        toast.className = 'toast show' + (isError ? ' error' : '');
        setTimeout(function () { toast.className = 'toast'; }, 3000);
    }
    </script>
</body>
</html>
