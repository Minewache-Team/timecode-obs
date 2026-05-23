<?php
/**
 * MW-Aufnahme System - Discord OAuth2 Callback
 */

if (session_status() === PHP_SESSION_NONE) {
    session_start();
}
require_once __DIR__ . '/includes/config.php';
require_once __DIR__ . '/includes/auth.php';

function abort(string $msg): never
{
    $_SESSION['discord_error'] = $msg;
    header('Location: login.php');
    exit;
}

/* ── State / CSRF prüfen ──────────────────────────────────────── */
$state = $_GET['state'] ?? '';
if (!$state || !hash_equals($_SESSION['discord_oauth_state'] ?? '', $state)) {
    abort('Ungültiger State-Parameter. Bitte nochmal versuchen.');
}
unset($_SESSION['discord_oauth_state']);

/* ── Discord-Fehler abfangen ──────────────────────────────────── */
if (isset($_GET['error'])) {
    abort('Discord: ' . htmlspecialchars($_GET['error_description'] ?? $_GET['error']));
}

/* ── Code einlösen ────────────────────────────────────────────── */
$code = $_GET['code'] ?? '';
if (!$code) {
    abort('Kein Authorization Code erhalten.');
}

$token = mw_discord_post('https://discord.com/api/oauth2/token', [
    'client_id' => DISCORD_CLIENT_ID,
    'client_secret' => DISCORD_CLIENT_SECRET,
    'grant_type' => 'authorization_code',
    'code' => $code,
    'redirect_uri' => DISCORD_REDIRECT_URI,
]);

if (empty($token['access_token'])) {
    abort('Token-Austausch fehlgeschlagen.');
}

/* ── Nutzer abrufen ───────────────────────────────────────────── */
$user = mw_discord_get('https://discord.com/api/users/@me', $token['access_token']);
if (empty($user['id'])) {
    abort('Nutzerinformationen konnten nicht abgerufen werden.');
}

/* ── Allowlist prüfen ─────────────────────────────────────────── */
if (!in_array($user['id'], DISCORD_ALLOWED_IDS, true)) {
    abort('Zugriff verweigert. Dein Discord-Account ist nicht autorisiert.');
}

/* ── Session anlegen ──────────────────────────────────────────── */
session_regenerate_id(true);
$_SESSION['mw_authenticated'] = true;
$_SESSION['discord_user_id'] = $user['id'];
$_SESSION['discord_username'] = $user['global_name'] ?? $user['username'] ?? 'Regisseur';

/* ── Weiterleiten ─────────────────────────────────────────────── */
$redirect = $_SESSION['discord_redirect_after'] ?? 'index.php';
unset($_SESSION['discord_redirect_after']);
if (preg_match('/^https?:/i', $redirect)) {
    $redirect = 'index.php';
}

header('Location: ' . $redirect);
exit;
