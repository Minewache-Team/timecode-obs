/**
 * MW-Aufnahme System - Frontend JavaScript
 *
 * Verbindet sich per SSE mit dem Server und aktualisiert das Dashboard.
 * "Szene laeuft"-Toggle sammelt alle Online-Sessions als Beteiligte.
 * Beim Deaktivieren erscheint das Szenen-Popup.
 */

(function () {
    'use strict';

    /* ---- State ---- */
    let eventSource = null;
    let sceneRunning = false;
    let sceneSessionIds = new Set();
    /* Per-Session Cooldown fuer den Resync-Button (TICKET-036) —
     * 30s, entspricht dem Heartbeat-Intervall des Plugins. */
    let resyncCooldown = {};

    /* ---- DOM References ---- */
    const userGrid = document.getElementById('user-grid');
    const onlineCount = document.getElementById('online-count');
    const lastUpdate = document.getElementById('last-update');
    const modalOverlay = document.getElementById('scene-modal');
    const sceneForm = document.getElementById('scene-form');
    const toast = document.getElementById('toast');
    const sceneToggle = document.getElementById('scene-toggle');
    const sceneStatus = document.getElementById('scene-status');

    /* ---- Scene Toggle ---- */

    if (sceneToggle) {
        sceneToggle.addEventListener('change', function () {
            if (sceneToggle.checked) {
                /* Szene startet — IDs sammeln ab jetzt */
                sceneRunning = true;
                sceneSessionIds = new Set();
                if (sceneStatus) sceneStatus.textContent = 'Szene laeuft...';
                showToast('Szene gestartet — Beteiligte werden erfasst');
            } else {
                /* Szene beendet — Popup zeigen */
                sceneRunning = false;
                if (sceneStatus) sceneStatus.textContent = '';
                if (sceneSessionIds.size > 0) {
                    showSceneModal(Array.from(sceneSessionIds));
                } else {
                    showToast('Keine Beteiligten erfasst', true);
                }
            }
        });
    }

    /* ---- SSE Connection ---- */

    function connectSSE() {
        if (eventSource) {
            eventSource.close();
        }

        eventSource = new EventSource('sse.php');

        eventSource.onmessage = function (event) {
            try {
                const data = JSON.parse(event.data);
                updateDashboard(data);
            } catch (e) {
                console.error('SSE parse error:', e);
            }
        };

        eventSource.addEventListener('timeout', function () {
            eventSource.close();
            setTimeout(connectSSE, 1000);
        });

        eventSource.onerror = function () {
            eventSource.close();
            setTimeout(connectSSE, 5000);
        };
    }

    /* ---- Dashboard Update ---- */

    function updateDashboard(data) {
        /* DB-Fehler vom Server anzeigen */
        if (data.error) {
            if (userGrid) {
                userGrid.innerHTML = '<div style="background:#3a1010;border:1px solid #f44336;'
                    + 'padding:15px;border-radius:8px;">'
                    + '<p style="color:#f44336;font-weight:bold;">Datenbankfehler</p>'
                    + '<p style="color:#ccc;">' + escapeHtml(data.error) + '</p>'
                    + '<p style="color:#888;margin-top:10px;">Wurde <a href="install.php" '
                    + 'style="color:#4fc3f7;">install.php</a> schon aufgerufen?</p></div>';
            }
            if (lastUpdate) {
                lastUpdate.textContent = 'Fehler';
            }
            return;
        }

        const newOnlineCount = data.online_count || 0;
        const sessions = data.sessions || [];

        /* Status-Leiste aktualisieren */
        if (onlineCount) {
            onlineCount.textContent = newOnlineCount;
        }
        if (lastUpdate) {
            lastUpdate.textContent = data.timestamp || '';
        }

        /* User-Karten rendern */
        if (userGrid) {
            renderUserCards(sessions);
        }

        /* Waehrend "Szene laeuft": alle Online-Sessions sammeln */
        if (sceneRunning) {
            sessions.forEach(function (s) {
                if (s.status === 'online') {
                    sceneSessionIds.add(s.id);
                }
            });
            /* Zaehler im Toggle-Label aktualisieren */
            if (sceneStatus) {
                sceneStatus.textContent = 'Szene laeuft... (' + sceneSessionIds.size + ' Beteiligte)';
            }
        }
    }

    function renderUserCards(sessions) {
        if (!userGrid) return;

        let html = '';

        if (sessions.length === 0) {
            html = '<p class="empty-state">Keine Aufnahmen in den letzten 24 Stunden.</p>';
        } else {
            sessions.forEach(function (s) {
                const isOnline = s.status === 'online';
                const statusClass = isOnline ? 'online' : 'offline';
                const statusText = isOnline ? 'Aufnahme laeuft' : 'Offline';
                const timeStr = isOnline
                    ? formatTime(s.started_at)
                    : (s.stopped_at ? formatTime(s.stopped_at) : '-');
                const timeLabel = isOnline ? 'Seit' : 'Gestoppt';

                /* Offset / Sync-Method (TICKET-035) — nur sichtbar bei Online */
                const offsetMs = (s.offset_ms !== null && s.offset_ms !== undefined && s.offset_ms !== '')
                    ? parseInt(s.offset_ms, 10) : null;
                const syncMethod = (s.sync_method !== null && s.sync_method !== undefined && s.sync_method !== '')
                    ? parseInt(s.sync_method, 10) : null;
                const offsetCls = (isOnline && offsetMs !== null) ? offsetSeverity(offsetMs) : '';
                const offsetLine = (isOnline && offsetMs !== null)
                    ? '<br>Drift: ' + formatOffset(offsetMs)
                    + (syncMethod ? ' <span class="sync-label">(' + syncMethodLabel(syncMethod) + ')</span>' : '')
                    : '';

                /* Plugin-Version (TICKET-047) — immer sichtbar wenn vorhanden.
                 * Veraltete Versionen werden orange markiert, fehlende grau. */
                const pluginVersion = (s.plugin_version !== null && s.plugin_version !== undefined && s.plugin_version !== '')
                    ? String(s.plugin_version) : null;
                const latestVer = window.MW_LATEST_PLUGIN_VERSION || '';
                let versionCls = 'version-unknown';
                let versionTxt = 'v? (nicht gemeldet)';
                if (pluginVersion !== null) {
                    if (latestVer && pluginVersion !== latestVer) {
                        versionCls = 'version-outdated';
                        versionTxt = 'v' + escapeHtml(pluginVersion) + ' (Update verfuegbar)';
                    } else {
                        versionCls = 'version-current';
                        versionTxt = 'v' + escapeHtml(pluginVersion);
                    }
                }
                const versionLine = '<br><span class="' + versionCls + '">Plugin: ' + versionTxt + '</span>';

                /* Letzte-Sync-Zeit (TICKET-051) — nur sichtbar bei Online,
                 * mit Farbcode bei Staleness. -1 = nie gesynct (rot). */
                const ageRaw = (s.offset_age_sec !== null && s.offset_age_sec !== undefined && s.offset_age_sec !== '')
                    ? parseInt(s.offset_age_sec, 10) : null;
                let lastSyncLine = '';
                if (isOnline && ageRaw !== null) {
                    let cls = 'sync-fresh';
                    let txt;
                    if (ageRaw < 0) {
                        cls = 'sync-old';
                        txt = 'nie gesynct';
                    } else if (ageRaw < 180) {
                        cls = 'sync-fresh';
                        txt = 'vor ' + ageRaw + ' s';
                    } else if (ageRaw < 480) {
                        cls = 'sync-stale';
                        txt = 'vor ' + Math.round(ageRaw / 60) + ' Min';
                    } else {
                        cls = 'sync-old';
                        txt = 'vor ' + Math.round(ageRaw / 60) + ' Min';
                    }
                    lastSyncLine = '<br><span class="' + cls + '">Letzte Sync: ' + txt + '</span>';
                }

                const stopBtn = isOnline
                    ? '<button class="force-stop-btn" title="Aufnahme erzwungen beenden" onclick="event.stopPropagation();forceStop(' + s.id + ',\'' + escapeHtml(s.user_name).replace(/'/g, "\\'") + '\')">&#9632; Beenden</button>'
                    : '';

                /* Re-Sync-Button (TICKET-036) — drei Zustaende abhaengig
                 * von Status + last_recording_active. Hard-Resync waehrend
                 * Aufnahme wuerde den Timecode zerstoeren (TICKET-008), das
                 * Backend wartet daher bis zum Idle-Heartbeat. */
                const recActive = parseInt(s.last_recording_active || 0, 10) === 1;
                const pending = parseInt(s.pending_resync || 0, 10) === 1;
                let resyncBtn = '';
                if (isOnline) {
                    const cooled = resyncCooldown[s.id] && (Date.now() - resyncCooldown[s.id] < 30000);
                    const label = pending
                        ? '↻ Resync angefragt'
                        : (recActive
                            ? '↻ Resync nach Aufnahme'
                            : '↻ Resync jetzt');
                    const cls = 'resync-btn'
                        + (recActive ? ' resync-queued' : '')
                        + ((cooled || pending) ? ' disabled' : '');
                    const disabled = (cooled || pending) ? 'disabled' : '';
                    const title = recActive
                        ? 'Resync wird nach Aufnahmestop ausgefuehrt (Hard-Resync waehrend Aufnahme wuerde den Timecode zerstoeren)'
                        : 'NTP-Resync sofort auf diesem Plugin ausloesen';
                    resyncBtn = '<button class="' + cls + '" ' + disabled
                        + ' title="' + title + '"'
                        + ' onclick="event.stopPropagation();requestResync(' + s.id + ',\'' + escapeHtml(s.user_name).replace(/'/g, "\\'") + '\')">'
                        + label + '</button>';
                }

                const cardClass = ('user-card ' + statusClass + (offsetCls ? ' ' + offsetCls : '')).trim();

                html += '<div class="' + cardClass + '">'
                    + '<button class="delete-btn" title="User entfernen" onclick="event.stopPropagation();deleteSession(' + s.id + ',\'' + escapeHtml(s.user_name).replace(/'/g, "\\'") + '\')">&times;</button>'
                    + '<div class="name">'
                    + '<span class="status-dot"></span>'
                    + escapeHtml(s.user_name)
                    + '</div>'
                    + '<div class="details">'
                    + 'Kamera ' + escapeHtml(s.camera_id)
                    + ' &middot; ' + statusText
                    + '<br>' + timeLabel + ': ' + timeStr
                    + offsetLine
                    + lastSyncLine
                    + versionLine
                    + '</div>'
                    + resyncBtn
                    + stopBtn
                    + '</div>';
            });
        }

        userGrid.innerHTML = html;
    }

    /* Offset-Formatierung: ms unter 1s, sonst Sekunden mit 1 Nachkommastelle */
    function formatOffset(ms) {
        const abs = Math.abs(ms);
        if (abs < 1000) return ms + 'ms';
        const sign = ms < 0 ? '-' : '';
        return sign + (abs / 1000).toFixed(1) + 's';
    }

    /* sync_method int -> Label (matches sync_method_t in ltc-source.h) */
    function syncMethodLabel(m) {
        switch (m) {
            case 1: return 'NTP';
            case 2: return 'HTTP';
            case 3: return 'Local';
            default: return '';
        }
    }

    /* CSS-Klassen-Mapping:
     * |offset| <= 1000ms  -> '' (neutral, gruener Rand vom Online-Status)
     * 1000 < |offset| <= 5000  -> 'offset-warn' (orange)
     * |offset| > 5000     -> 'offset-crit' (rot + Puls) */
    function offsetSeverity(ms) {
        const abs = Math.abs(ms);
        if (abs > 5000) return 'offset-crit';
        if (abs > 1000) return 'offset-warn';
        return '';
    }

    /* ---- Scene Modal ---- */

    function isModalOpen() {
        return modalOverlay && modalOverlay.classList.contains('active');
    }

    function showSceneModal(sessionIds) {
        if (!modalOverlay) return;
        if (isModalOpen()) return;

        const idsInput = document.getElementById('session-ids');
        if (idsInput) {
            idsInput.value = sessionIds.join(',');
        }

        modalOverlay.classList.add('active');
    }

    function hideSceneModal() {
        if (modalOverlay) {
            modalOverlay.classList.remove('active');
        }
    }

    if (sceneForm) {
        sceneForm.addEventListener('submit', function (e) {
            e.preventDefault();

            const formData = new FormData(sceneForm);

            fetch('scene-save.php', {
                method: 'POST',
                body: formData,
            })
                .then(function (res) { return res.json(); })
                .then(function (data) {
                    if (data.ok) {
                        showToast('Szene gespeichert!');
                        hideSceneModal();
                        sceneForm.reset();
                        /* Reset Take auf 1 nach dem Speichern */
                        var takeInput = document.getElementById('take');
                        if (takeInput) takeInput.value = '1';
                    } else {
                        showToast(data.error || 'Fehler beim Speichern', true);
                    }
                })
                .catch(function () {
                    showToast('Netzwerkfehler', true);
                });
        });
    }

    /* Modal schliessen per Abbrechen-Button */
    const cancelBtn = document.getElementById('modal-cancel');
    if (cancelBtn) {
        cancelBtn.addEventListener('click', function () {
            hideSceneModal();
            sceneSessionIds = new Set();
        });
    }

    /* ---- Toast ---- */

    function showToast(message, isError) {
        if (!toast) return;
        toast.textContent = message;
        toast.className = 'toast show' + (isError ? ' error' : '');
        setTimeout(function () {
            toast.className = 'toast';
        }, 3000);
    }

    /* ---- Helpers ---- */

    function escapeHtml(str) {
        var div = document.createElement('div');
        div.appendChild(document.createTextNode(str || ''));
        return div.innerHTML;
    }

    function formatTime(dateStr) {
        if (!dateStr) return '-';
        try {
            var d = new Date(dateStr);
            return d.toLocaleTimeString('de-DE', {
                hour: '2-digit',
                minute: '2-digit',
                second: '2-digit',
            });
        } catch (e) {
            return dateStr;
        }
    }

    /* ---- Force Stop (Aufnahme erzwungen beenden) ---- */

    window.forceStop = function (sessionId, userName) {
        if (!confirm('"' + userName + '" ist noch als Online markiert.\n\nAufnahme trotzdem erzwungen beenden?')) {
            return;
        }

        fetch('dashboard-api.php?action=force_stop', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ session_id: sessionId }),
        })
            .then(function (res) { return res.json(); })
            .then(function (data) {
                if (data.ok) {
                    showToast(data.message || 'Aufnahme beendet');
                } else {
                    showToast(data.error || 'Fehler', true);
                }
            })
            .catch(function () {
                showToast('Netzwerkfehler', true);
            });
    };

    /* ---- Re-Sync (NTP-Resync auf einem Plugin ausloesen) ---- */

    window.requestResync = function (sessionId, userName) {
        /* Lokaler Cooldown: 30s nach Klick die Schaltflaeche sperren */
        resyncCooldown[sessionId] = Date.now();

        fetch('dashboard-api.php?action=request_resync', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ session_id: sessionId }),
        })
            .then(function (res) { return res.json(); })
            .then(function (data) {
                if (data.ok) {
                    showToast(data.message || 'Resync angefragt');
                } else {
                    /* Fehler: Cooldown sofort wieder freigeben */
                    delete resyncCooldown[sessionId];
                    showToast(data.error || 'Fehler', true);
                }
            })
            .catch(function () {
                delete resyncCooldown[sessionId];
                showToast('Netzwerkfehler', true);
            });
    };

    /* ---- Delete Session (User entfernen) ---- */

    window.deleteSession = function (sessionId, userName) {
        if (!confirm('User "' + userName + '" wirklich entfernen?\n\nBeim naechsten Verbinden erscheint er automatisch wieder.')) {
            return;
        }

        fetch('dashboard-api.php?action=delete_session', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ session_id: sessionId }),
        })
            .then(function (res) { return res.json(); })
            .then(function (data) {
                if (data.ok) {
                    showToast(data.message || 'User entfernt');
                } else {
                    showToast(data.error || 'Fehler', true);
                }
            })
            .catch(function () {
                showToast('Netzwerkfehler', true);
            });
    };

    /* ---- Delete Scene ---- */

    window.deleteScene = function (sceneId, sceneName) {
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
                    /* Zeile aus der Tabelle entfernen */
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
    };

    /* ---- Init ---- */
    connectSSE();
})();
