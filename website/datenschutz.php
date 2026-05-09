<?php
/**
 * MW-Aufnahme System - Datenschutzerklärung (DSGVO)
 *
 * Diese Seite ist OHNE Login erreichbar, damit jeder Teilnehmer
 * die Datenschutzerklärung lesen kann, bevor er seine Einwilligung gibt.
 * Link kann direkt an Teilnehmer gesendet werden.
 */

require_once __DIR__ . '/includes/config.php';
?>
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Datenschutzerklärung - MW Aufnahme-System</title>
    <link rel="stylesheet" href="assets/style.css">
    <style>
        .privacy { max-width: 750px; line-height: 1.8; }
        .privacy h2 {
            color: #4fc3f7; margin-top: 30px; margin-bottom: 12px;
            font-size: 1.15rem; border-bottom: 1px solid #333; padding-bottom: 6px;
        }
        .privacy h3 { color: #81d4fa; margin-top: 18px; margin-bottom: 8px; font-size: 1rem; }
        .privacy p, .privacy ul, .privacy ol { margin-bottom: 14px; color: #ccc; font-size: 0.95rem; }
        .privacy ul, .privacy ol { padding-left: 25px; }
        .privacy li { margin-bottom: 6px; }
        .privacy .placeholder { color: #f44336; font-weight: bold; }
        .privacy .highlight { background: #0f3460; padding: 15px; border-radius: 8px; margin: 15px 0; }
        .privacy .highlight p { margin-bottom: 6px; }
        .privacy table { width: 100%; border-collapse: collapse; margin: 15px 0; }
        .privacy table th, .privacy table td {
            padding: 10px 12px; text-align: left; border-bottom: 1px solid #333;
            font-size: 0.9rem;
        }
        .privacy table th { background: #0f3460; color: #4fc3f7; font-weight: 600; }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>Datenschutzerklärung</h1>
            <p style="color: #888; font-size: 0.9rem; margin-top: 5px;">
                MW Aufnahme-System &ndash; Informationen zum Datenschutz gemäß DSGVO
            </p>
        </header>

        <div class="privacy">

            <!-- ============================================================ -->
            <h2>1. Verantwortlicher</h2>
            <p>
                Verantwortlich für die Datenverarbeitung im Sinne der
                Datenschutz-Grundverordnung (DSGVO) ist:
            </p>
            <div class="highlight">
                <p><strong><?= htmlspecialchars(OPERATOR_NAME) ?></strong></p>
                <p><?= htmlspecialchars(OPERATOR_ADDRESS) ?></p>
                <p>E-Mail: <a href="mailto:<?= htmlspecialchars(OPERATOR_EMAIL) ?>" style="color:#4fc3f7;">
                    <?= htmlspecialchars(OPERATOR_EMAIL) ?></a></p>
            </div>
            <?php if (OPERATOR_NAME === '[Dein Name]'): ?>
                <p class="placeholder">
                    &#9888; ACHTUNG: Die Angaben des Verantwortlichen müssen noch
                    in der Konfiguration (<code>includes/config.php</code>) eingetragen werden.
                    Ohne korrekte Angaben ist diese Datenschutzerklärung unvollständig.
                </p>
            <?php endif; ?>

            <!-- ============================================================ -->
            <h2>2. Beschreibung der Verarbeitung</h2>

            <h3>2.1 Was ist das MW Aufnahme-System?</h3>
            <p>
                Das MW Aufnahme-System ist ein internes Koordinations-Tool für
                Multi-Kamera-Produktionen. Es besteht aus zwei Komponenten:
            </p>
            <ol>
                <li>Einem <strong>OBS-Studio-Plugin</strong>, das auf den Rechnern
                    der Kameraleute installiert wird und bei Aufnahmebeginn/-ende
                    ein Signal an einen zentralen Server sendet.</li>
                <li>Einem <strong>passwortgeschützten Web-Dashboard</strong>,
                    auf dem <strong>ausschließlich der Regisseur</strong> nach
                    Login in Echtzeit sehen kann, welche Kameras gerade
                    aufnehmen.</li>
            </ol>
            <p>
                Es werden <strong>keinerlei Aufnahmeinhalte</strong> (kein Audio,
                kein Video, keine Bildschirminhalte) übertragen oder gespeichert.
                Es wird lediglich der Status &bdquo;Aufnahme läuft&ldquo; bzw.
                &bdquo;Aufnahme gestoppt&ldquo; übermittelt.
            </p>

            <h3>2.2 Wer kann die Daten einsehen?</h3>
            <div class="highlight">
                <p>
                    <strong>Nur der Regisseur</strong> hat Zugang zum Dashboard
                    und zur Szenen-Übersicht. Das Dashboard ist durch eine
                    Login-Seite mit Benutzername und Passwort geschützt. Kein anderer
                    Teilnehmer, kein Kameramann und keine dritte Person kann
                    sehen, wer gerade aufnimmt oder wer in der Vergangenheit
                    aufgenommen hat.
                </p>
            </div>

            <!-- ============================================================ -->
            <h2>3. Welche Daten werden erhoben</h2>
            <p>
                Bei Nutzung des MW Aufnahme-Systems werden folgende
                personenbezogene Daten verarbeitet:
            </p>
            <table>
                <thead>
                    <tr>
                        <th>Datum</th>
                        <th>Beschreibung</th>
                        <th>Beispiel</th>
                    </tr>
                </thead>
                <tbody>
                    <tr>
                        <td><strong>Anzeigename</strong></td>
                        <td>Der von dir frei gewählte Name. Muss nicht
                            dein echter Name sein.</td>
                        <td>&bdquo;Kamera-Max&ldquo;</td>
                    </tr>
                    <tr>
                        <td><strong>Kamera-ID</strong></td>
                        <td>Die dir zugewiesene Kamerakennung (A&ndash;H).</td>
                        <td>&bdquo;B&ldquo;</td>
                    </tr>
                    <tr>
                        <td><strong>Aufnahmestatus</strong></td>
                        <td>Ob du gerade aufnimmst (online) oder nicht (offline).</td>
                        <td>&bdquo;online&ldquo;</td>
                    </tr>
                    <tr>
                        <td><strong>Zeitstempel</strong></td>
                        <td>Zeitpunkt von Aufnahmestart, -ende und letztem
                            Heartbeat-Signal.</td>
                        <td>&bdquo;2026-03-27 14:30:00&ldquo;</td>
                    </tr>
                    <tr>
                        <td><strong>IP-Adresse</strong></td>
                        <td>Wird <strong>ausschließlich</strong> im
                            Einwilligungs-Log gespeichert, um die
                            Einwilligung nachweisen zu können.</td>
                        <td>&bdquo;192.168.1.x&ldquo;</td>
                    </tr>
                </tbody>
            </table>

            <h3>3.1 Daten, die NICHT erhoben werden</h3>
            <ul>
                <li>Keine Audio- oder Videoinhalte der Aufnahme</li>
                <li>Keine Bildschirminhalte oder Screenshots</li>
                <li>Keine Standortdaten (GPS)</li>
                <li>Keine Geräteinformationen (Hardware, Betriebssystem)</li>
                <li>Keine Cookies oder Tracking-Technologien</li>
                <li>Keine Daten an Werbenetzwerke oder Analyse-Dienste</li>
            </ul>

            <!-- ============================================================ -->
            <h2>4. Zweck der Datenverarbeitung</h2>
            <p>Die erhobenen Daten dienen <strong>ausschließlich</strong> folgenden Zwecken:</p>
            <ol>
                <li><strong>Echtzeit-Koordination:</strong> Der Regisseur sieht
                    auf dem Dashboard, welche Kameras gerade aufnehmen, um die
                    Produktion zu koordinieren.</li>
                <li><strong>Szenen-Dokumentation:</strong> Nach jeder Aufnahme-Session
                    dokumentiert der Regisseur welche Szene (Staffel, Folge,
                    Szenenname) aufgenommen wurde. Dies dient der
                    Nachbearbeitung und dem Schnitt.</li>
                <li><strong>Nachweis der Einwilligung:</strong> Das Einwilligungs-Log
                    dient dem Nachweis, dass du der Datenverarbeitung zugestimmt
                    hast (DSGVO Art. 7 Abs. 1).</li>
            </ol>
            <p>
                Die Daten werden <strong>nicht</strong> für andere Zwecke
                verwendet, insbesondere nicht für Leistungsüberwachung,
                Verhaltensanalyse oder Profilbildung.
            </p>

            <!-- ============================================================ -->
            <h2>5. Rechtsgrundlage</h2>
            <p>
                Die Verarbeitung deiner personenbezogenen Daten erfolgt
                auf Grundlage deiner <strong>ausdrücklichen Einwilligung</strong>
                gemäß <strong>Art. 6 Abs. 1 lit. a DSGVO</strong>.
            </p>

            <h3>5.1 Wie die Einwilligung eingeholt wird</h3>
            <ol>
                <li>Du installierst das OBS-Plugin und trägst deinen
                    Anzeigenamen und die Server-URL in den Einstellungen ein.</li>
                <li>Beim <strong>ersten Start einer MW-Aufnahme</strong>
                    erscheint ein Dialogfenster, das dich über die
                    Datenübermittlung informiert und auf diese
                    Datenschutzerklärung verweist.</li>
                <li>Erst wenn du <strong>ausdrücklich zustimmst</strong> (&bdquo;Ja&ldquo;),
                    werden Daten an den Server übertragen.</li>
                <li>Wenn du <strong>ablehnst</strong> (&bdquo;Nein&ldquo;), werden
                    <strong>keine Daten übermittelt</strong>. Du kannst OBS
                    weiterhin normal nutzen.</li>
            </ol>

            <h3>5.2 Protokollierung der Einwilligung</h3>
            <p>
                Deine Einwilligung wird dokumentiert:
            </p>
            <ul>
                <li><strong>Lokal:</strong> In deiner OBS-Konfiguration (auf deinem Rechner).</li>
                <li><strong>Auf dem Server:</strong> Im Einwilligungs-Log mit
                    Anzeigename, Zeitstempel und IP-Adresse &ndash; ausschließlich
                    zum Nachweis der erteilten Einwilligung.</li>
            </ul>

            <!-- ============================================================ -->
            <h2>6. Speicherdauer und Löschung</h2>
            <div class="highlight">
                <p><strong>Wir speichern deine Daten nur so lange, wie es für
                    den jeweiligen Zweck erforderlich ist:</strong></p>
            </div>
            <table>
                <thead>
                    <tr>
                        <th>Datenart</th>
                        <th>Speicherdauer</th>
                        <th>Löschung</th>
                    </tr>
                </thead>
                <tbody>
                    <tr>
                        <td><strong>Aufnahme-Sessions</strong><br>
                            (Name, Kamera, Status, Zeitstempel)</td>
                        <td><strong>30 Tage</strong></td>
                        <td>Automatische Löschung nach 30 Tagen.
                            Auf Wunsch auch früher.</td>
                    </tr>
                    <tr>
                        <td><strong>Szenen-Dokumentation</strong><br>
                            (Staffel, Folge, Szenenname)</td>
                        <td>Bis <strong>Projektabschluss</strong></td>
                        <td>Wird nach Abschluss des Projekts
                            vollständig gelöscht.</td>
                    </tr>
                    <tr>
                        <td><strong>Einwilligungs-Log</strong><br>
                            (Name, IP, Zeitstempel)</td>
                        <td>Dauer der <strong>Zusammenarbeit</strong></td>
                        <td>Wird aufbewahrt, solange die Zusammenarbeit
                            besteht (gesetzliche Nachweispflicht
                            nach Art. 7 Abs. 1 DSGVO). Danach Löschung.</td>
                    </tr>
                </tbody>
            </table>
            <p>
                Sobald der Speicherzweck entfällt, werden die Daten
                unverzüglich gelöscht. Es erfolgt keine dauerhafte
                Archivierung.
            </p>

            <!-- ============================================================ -->
            <h2>7. Empfänger der Daten / Zugang</h2>
            <div class="highlight">
                <p><strong>Zugang zum Dashboard hat ausschließlich der Regisseur.</strong></p>
                <p>
                    Das Dashboard (Live-Status, Szenen-Archiv) ist durch
                    eine Login-Seite geschützt. Nur der Regisseur kennt die
                    Zugangsdaten und kann einsehen:
                </p>
                <ul style="margin-top: 8px;">
                    <li>Welche Kameras gerade aufnehmen (Echtzeit)</li>
                    <li>Welche Szenen dokumentiert wurden</li>
                </ul>
            </div>
            <p>
                <strong>Kein anderer Teilnehmer</strong> (Kameramann, Schauspieler,
                sonstige Beteiligte) hat Zugang zu diesen Informationen.
            </p>
            <p>
                <strong>Keine Weitergabe an Dritte:</strong> Deine Daten werden
                nicht an Dritte weitergegeben, verkauft, vermietet oder
                für andere Zwecke verwendet. Es gibt keine Einbindung
                von Drittanbieter-Diensten (kein Google Analytics, kein
                Facebook Pixel, keine Werbenetzwerke).
            </p>

            <!-- ============================================================ -->
            <h2>8. Deine Rechte (Betroffenenrechte)</h2>
            <p>
                Du hast gemäß DSGVO jederzeit folgende Rechte. Zur
                Ausübung genügt eine formlose E-Mail an
                <strong><a href="mailto:<?= htmlspecialchars(OPERATOR_EMAIL) ?>" style="color:#4fc3f7;">
                <?= htmlspecialchars(OPERATOR_EMAIL) ?></a></strong>:
            </p>
            <table>
                <thead>
                    <tr>
                        <th>Recht</th>
                        <th>Artikel</th>
                        <th>Was bedeutet das?</th>
                    </tr>
                </thead>
                <tbody>
                    <tr>
                        <td><strong>Auskunft</strong></td>
                        <td>Art. 15</td>
                        <td>Du kannst erfahren, welche Daten über dich
                            gespeichert sind.</td>
                    </tr>
                    <tr>
                        <td><strong>Berichtigung</strong></td>
                        <td>Art. 16</td>
                        <td>Du kannst die Korrektur falscher Daten verlangen.</td>
                    </tr>
                    <tr>
                        <td><strong>Löschung</strong></td>
                        <td>Art. 17</td>
                        <td>Du kannst die sofortige Löschung aller deiner
                            Daten verlangen (&bdquo;Recht auf Vergessenwerden&ldquo;).</td>
                    </tr>
                    <tr>
                        <td><strong>Einschränkung</strong></td>
                        <td>Art. 18</td>
                        <td>Du kannst verlangen, dass deine Daten nur
                            eingeschränkt verarbeitet werden.</td>
                    </tr>
                    <tr>
                        <td><strong>Datenportabilität</strong></td>
                        <td>Art. 20</td>
                        <td>Du kannst deine Daten in einem gängigen,
                            maschinenlesbaren Format (JSON) erhalten.</td>
                    </tr>
                    <tr>
                        <td><strong>Widerspruch</strong></td>
                        <td>Art. 21</td>
                        <td>Du kannst der Verarbeitung deiner Daten
                            widersprechen.</td>
                    </tr>
                    <tr>
                        <td><strong>Beschwerde</strong></td>
                        <td>Art. 77</td>
                        <td>Du hast das Recht, dich bei einer
                            Datenschutz-Aufsichtsbehörde zu beschweren.</td>
                    </tr>
                </tbody>
            </table>
            <p>
                Wir bearbeiten deine Anfrage <strong>innerhalb von 30 Tagen</strong>
                kostenfrei (Art. 12 Abs. 3 DSGVO).
            </p>

            <!-- ============================================================ -->
            <h2>9. Widerruf der Einwilligung</h2>
            <div class="highlight">
                <p>
                    Du kannst deine Einwilligung <strong>jederzeit und ohne
                    Angabe von Gründen</strong> widerrufen.
                </p>
            </div>

            <h3>9.1 Wie widerrufst du?</h3>
            <ul>
                <li><strong>Per E-Mail:</strong> Schreibe formlos an
                    <a href="mailto:<?= htmlspecialchars(OPERATOR_EMAIL) ?>" style="color:#4fc3f7;">
                    <?= htmlspecialchars(OPERATOR_EMAIL) ?></a>.</li>
                <li><strong>Im OBS-Plugin:</strong> Deaktiviere die Option
                    &bdquo;MW Aufnahme aktivieren&ldquo; unter
                    Werkzeuge &rarr; MW Aufnahme. Danach werden keine Daten
                    mehr übertragen.</li>
            </ul>

            <h3>9.2 Was passiert nach dem Widerruf?</h3>
            <ul>
                <li>Es werden <strong>sofort keine weiteren Daten</strong> mehr
                    übertragen.</li>
                <li>Sämtliche zu deiner Person gespeicherten Daten werden
                    <strong>unverzüglich gelöscht</strong> (Sessions,
                    Szenen-Zuordnungen).</li>
                <li>Das Einwilligungs-Log wird mit dem Vermerk
                    &bdquo;widerrufen&ldquo; aktualisiert und nach
                    Ablauf der Nachweispflicht gelöscht.</li>
                <li>Die Rechtmäßigkeit der bis zum Widerruf erfolgten
                    Verarbeitung bleibt davon <strong>unberührt</strong>
                    (Art. 7 Abs. 3 DSGVO).</li>
            </ul>
            <p>
                Du kannst OBS Studio und alle anderen Funktionen nach dem
                Widerruf weiterhin uneingeschränkt nutzen.
            </p>

            <!-- ============================================================ -->
            <h2>10. Datensicherheit (technische und organisatorische Maßnahmen)</h2>
            <p>
                Wir setzen folgende Maßnahmen zum Schutz deiner Daten ein:
            </p>
            <ul>
                <li><strong>Transportverschlüsselung:</strong> Alle Daten
                    zwischen OBS-Plugin und Server werden über
                    <strong>HTTPS (TLS)</strong> verschlüsselt übertragen.</li>
                <li><strong>Zugangskontrolle:</strong> Das Dashboard ist
                    durch eine <strong>Login-Seite</strong> mit Benutzername
                    und Passwort geschützt. Nur der Regisseur hat Zugang.</li>
                <li><strong>API-Authentifizierung:</strong> Die Schnittstelle
                    zum OBS-Plugin ist durch einen <strong>API-Key</strong>
                    geschützt. Ohne gültigem Key werden keine Daten
                    angenommen.</li>
                <li><strong>SQL-Injection-Schutz:</strong> Alle Datenbankzugriffe
                    erfolgen über <strong>Prepared Statements</strong> (PDO).</li>
                <li><strong>Eingabevalidierung:</strong> Alle übertragenen Daten
                    werden serverseitig validiert und bereinigt.</li>
                <li><strong>Datenminimierung:</strong> Es werden nur die für den
                    Zweck unbedingt erforderlichen Daten erhoben (Art. 5
                    Abs. 1 lit. c DSGVO).</li>
                <li><strong>Automatische Löschung:</strong> Sessions werden
                    nach 30 Tagen automatisch aus der Datenbank entfernt.</li>
            </ul>

            <!-- ============================================================ -->
            <h2>11. Hosting und Serverstandort</h2>
            <p>
                Die Website wird bei <strong>All-Inkl.com</strong> gehostet,
                einem deutschen Hosting-Anbieter mit Serverstandort in
                <strong>Deutschland</strong>.
            </p>
            <p>
                Es findet <strong>keine Übermittlung</strong> deiner Daten
                in Drittländer (außerhalb der EU/des EWR) statt.
            </p>
            <p>
                Informationen zum Datenschutz des Hosting-Anbieters:
                All-Inkl.com verarbeitet im Rahmen des Hostings Server-Logdateien
                (IP-Adresse, Zeitpunkt des Zugriffs, angeforderte Datei).
                Details dazu findest du in der Datenschutzerklärung von
                All-Inkl.com.
            </p>

            <!-- ============================================================ -->
            <h2>12. Cookies</h2>
            <p>
                Das Dashboard verwendet ein <strong>technisch notwendiges
                Session-Cookie</strong> (<code>PHPSESSID</code>), um den
                Regisseur nach dem Login angemeldet zu halten. Dieses Cookie:
            </p>
            <ul>
                <li>Wird <strong>nur beim Login</strong> auf dem Dashboard gesetzt</li>
                <li>Enthält <strong>keine personenbezogenen Daten</strong>,
                    sondern lediglich eine zufällige Session-ID</li>
                <li>Wird beim Schließen des Browsers automatisch gelöscht</li>
                <li>Ist <strong>technisch zwingend erforderlich</strong> für die
                    Funktion des Dashboards (Art. 6 Abs. 1 lit. f DSGVO)</li>
            </ul>
            <p>
                Darüber hinaus werden <strong>keine weiteren Cookies</strong>
                gesetzt. Es gibt keine Tracking-Pixel, kein Google Analytics
                und keine sonstigen Analyse-Tools. Es findet keinerlei
                Nutzer-Tracking statt.
            </p>

            <!-- ============================================================ -->
            <h2>13. Änderungen dieser Datenschutzerklärung</h2>
            <p>
                Wir behalten uns vor, diese Datenschutzerklärung bei
                Bedarf anzupassen, um sie an geänderte Rechtslagen oder
                Änderungen des Systems anzupassen. Die jeweils aktuelle
                Fassung ist stets unter dieser URL abrufbar. Bei wesentlichen
                Änderungen wirst du erneut um Einwilligung gebeten.
            </p>

            <!-- ============================================================ -->
            <h2>14. Aufsichtsbehörde</h2>
            <p>
                Wenn du der Meinung bist, dass die Verarbeitung deiner
                Daten gegen die DSGVO verstößt, hast du das Recht,
                dich bei einer Datenschutz-Aufsichtsbehörde zu beschweren
                (Art. 77 DSGVO). Zuständig ist in der Regel die
                Aufsichtsbehörde deines Bundeslandes.
            </p>
            <p>
                Eine Liste der Aufsichtsbehörden findest du unter:<br>
                <span style="color: #4fc3f7;">
                    www.bfdi.bund.de &rarr; Infothek &rarr; Anschriften und Links
                </span>
            </p>

            <!-- ============================================================ -->
            <!-- ============================================================ -->
            <h2>15. Freiwilligkeit und Folgen der Nichtbereitstellung</h2>
            <p>
                Die Bereitstellung deiner Daten ist <strong>vollständig
                freiwillig</strong>. Du bist weder gesetzlich noch vertraglich
                verpflichtet, die MW-Aufnahme zu nutzen oder deine Einwilligung
                zu erteilen.
            </p>
            <p>
                <strong>Folge bei Nichtbereitstellung:</strong> Wenn du die
                Einwilligung nicht erteilst oder widerrufst, kann der Regisseur
                deinen Aufnahmestatus nicht im Dashboard sehen. Du kannst
                OBS Studio und alle anderen Funktionen weiterhin
                uneingeschränkt nutzen. Es entstehen dir keinerlei Nachteile.
            </p>

            <!-- ============================================================ -->
            <h2>16. Automatisierte Entscheidungsfindung</h2>
            <p>
                Es findet <strong>keine automatisierte Entscheidungsfindung</strong>
                und <strong>kein Profiling</strong> im Sinne von Art. 22 DSGVO
                statt. Alle Daten werden ausschließlich zur Anzeige im
                Dashboard verwendet und nicht für automatisierte Auswertungen
                oder Bewertungen herangezogen.
            </p>

            <!-- ============================================================ -->
            <p style="margin-top: 40px; padding-top: 15px; border-top: 1px solid #333; color: #666; font-size: 0.85rem;">
                Stand dieser Datenschutzerklärung: <strong>31.03.2026</strong>
            </p>

        </div>
    </div>
</body>
</html>
