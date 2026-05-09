<?php
/**
 * MW-Aufnahme System - Dashboard
 *
 * Zeigt den Live-Status aller Kameraleute an.
 * Updates kommen per Server-Sent Events (SSE).
 * Zugang nur für den Regisseur (Passwortschutz).
 */

require_once __DIR__ . '/includes/db.php';
require_dashboard_auth();
?>
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MW Aufnahme - Dashboard</title>
    <link rel="stylesheet" href="assets/style.css">
</head>
<body>
    <div class="container">
        <header>
            <h1>MW Aufnahme</h1>
            <nav>
                <a href="index.php" class="active">Dashboard</a>
                <a href="scenes.php">Szenen / Statistik</a>
                <a href="datenschutz.php">Datenschutz</a>
                <a href="login.php?logout=1" style="color:#f44336;">Logout</a>
            </nav>
        </header>

        <div class="status-bar">
            <span>Online: <span class="count" id="online-count">0</span></span>
            <label class="scene-toggle-label">
                <input type="checkbox" id="scene-toggle">
                <span class="scene-toggle-slider"></span>
                <span class="scene-toggle-text">Szene laeuft</span>
            </label>
            <span class="scene-status" id="scene-status"></span>
            <span class="timestamp" id="last-update">Verbinde...</span>
        </div>

        <div class="user-grid" id="user-grid">
            <p class="empty-state">Verbinde mit Server...</p>
        </div>
    </div>

    <!-- Szenen-Popup (erscheint wenn alle offline gehen) -->
    <div class="modal-overlay" id="scene-modal">
        <div class="modal">
            <h2>Welche Szene war das?</h2>
            <form id="scene-form">
                <input type="hidden" name="session_ids" id="session-ids" value="">

                <label for="season">Staffel</label>
                <input type="number" name="season" id="season" min="1" value="1" required>

                <label for="episode">Folge</label>
                <input type="number" name="episode" id="episode" min="1" value="1" required>

                <label for="scene_name">Szenenname (optional)</label>
                <input type="text" name="scene_name" id="scene_name" placeholder="z.B. Intro, Interview, Outro...">

                <label for="take">Versuch</label>
                <input type="number" name="take" id="take" min="1" value="1" required>

                <label for="notes">Notizen (optional)</label>
                <textarea name="notes" id="notes" placeholder="Zusätzliche Infos..."></textarea>

                <div class="btn-row">
                    <button type="button" class="btn btn-secondary" id="modal-cancel">Abbrechen</button>
                    <button type="submit" class="btn btn-primary">Speichern</button>
                </div>
            </form>
        </div>
    </div>

    <!-- Toast Benachrichtigung -->
    <div class="toast" id="toast"></div>

    <script src="assets/app.js?v=2"></script>
</body>
</html>
