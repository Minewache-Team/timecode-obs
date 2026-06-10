<?php
/**
 * MW-Aufnahme System - Login (Discord OAuth2)
 */

if (session_status() === PHP_SESSION_NONE) { session_start(); }
require_once __DIR__ . '/includes/config.php';
require_once __DIR__ . '/includes/auth.php';

/* Bereits eingeloggt? */
if (!empty($_SESSION['mw_authenticated'])) {
    header('Location: index.php');
    exit;
}

/* Redirect-Ziel speichern und Discord-URL bauen */
$redirect = $_GET['redirect'] ?? '';
if ($redirect && !preg_match('/^https?:/i', $redirect)) {
    $_SESSION['discord_redirect_after'] = $redirect;
}

$discordUrl = mw_build_discord_auth_url();
$error      = $_SESSION['discord_error'] ?? '';
unset($_SESSION['discord_error']);
?>
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MW Aufnahme – Anmelden</title>
    <link rel="stylesheet" href="assets/style.css">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
    <style>
        *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

        body {
            font-family: 'Inter', system-ui, sans-serif;
            background: #0d1117;
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
            position: relative;
            overflow: hidden;
        }
        body::before {
            content: '';
            position: fixed;
            inset: 0;
            background:
                radial-gradient(ellipse 55% 40% at 15% 25%, rgba(88,101,242,.16) 0%, transparent 65%),
                radial-gradient(ellipse 45% 35% at 85% 75%, rgba(79,195,247,.10) 0%, transparent 60%);
            pointer-events: none;
        }

        .card {
            position: relative;
            background: rgba(22, 27, 34, 0.94);
            border: 1px solid rgba(88,101,242,.22);
            border-radius: 20px;
            padding: 2.8rem 2.4rem;
            width: 100%;
            max-width: 400px;
            box-shadow: 0 20px 60px rgba(0,0,0,.5), 0 0 80px rgba(88,101,242,.07);
            backdrop-filter: blur(14px);
            animation: rise .4s ease both;
        }
        @keyframes rise {
            from { opacity: 0; transform: translateY(20px); }
            to   { opacity: 1; transform: translateY(0); }
        }

        .badge {
            display: inline-flex;
            align-items: center;
            gap: 6px;
            background: rgba(79,195,247,.12);
            border: 1px solid rgba(79,195,247,.3);
            color: #4fc3f7;
            font-size: 10.5px;
            font-weight: 600;
            letter-spacing: .07em;
            text-transform: uppercase;
            padding: 3px 11px;
            border-radius: 999px;
            margin-bottom: 1.4rem;
        }
        .badge::before {
            content: '';
            width: 5px; height: 5px;
            background: #4fc3f7;
            border-radius: 50%;
            animation: blink 1.8s infinite;
        }
        @keyframes blink { 0%,100%{opacity:1} 50%{opacity:.3} }

        .title {
            color: #e6edf3;
            font-size: 1.4rem;
            font-weight: 700;
            margin-bottom: 4px;
        }
        .subtitle {
            color: #7d8590;
            font-size: 13px;
            margin-bottom: 1.8rem;
        }

        .divider {
            height: 1px;
            background: linear-gradient(90deg, transparent, rgba(88,101,242,.2), transparent);
            margin: 1.4rem 0;
        }

        .desc {
            color: #7d8590;
            font-size: 13px;
            line-height: 1.65;
            margin-bottom: 1.6rem;
        }
        .desc strong { color: #c9d1d9; }

        .error-box {
            background: rgba(248,81,73,.1);
            border: 1px solid rgba(248,81,73,.35);
            color: #f85149;
            border-radius: 9px;
            padding: .7rem 1rem;
            font-size: 13px;
            margin-bottom: 1.2rem;
            display: flex;
            gap: 8px;
            align-items: flex-start;
            animation: shake .28s ease;
        }
        @keyframes shake {
            0%,100%{transform:translateX(0)} 25%{transform:translateX(-4px)} 75%{transform:translateX(4px)}
        }

        .btn-discord {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 11px;
            width: 100%;
            padding: 13px 18px;
            background: #5865f2;
            color: #fff;
            font-family: inherit;
            font-size: 14.5px;
            font-weight: 600;
            border: none;
            border-radius: 11px;
            cursor: pointer;
            text-decoration: none;
            transition: background .17s, transform .14s, box-shadow .17s;
            box-shadow: 0 4px 18px rgba(88,101,242,.4);
            letter-spacing: .01em;
        }
        .btn-discord:hover {
            background: #4752c4;
            transform: translateY(-2px);
            box-shadow: 0 8px 28px rgba(88,101,242,.5);
        }
        .btn-discord:active { transform: translateY(0); }

        .footer-note {
            text-align: center;
            color: #484f58;
            font-size: 11px;
            margin-top: 1.4rem;
            line-height: 1.6;
        }
        .footer-note a {
            color: #484f58;
            text-decoration: underline;
        }
        .footer-note a:hover { color: #7d8590; }
    </style>
</head>
<body>
    <div class="card">
        <div class="badge">Regisseur-Zugang</div>

        <div class="title">MW Aufnahme</div>
        <div class="subtitle">Nur für autorisiertes Personal</div>

        <div class="divider"></div>

        <p class="desc">
            Das Dashboard ist <strong>ausschließlich für den Regisseur</strong> zugänglich.
            Die Anmeldung erfolgt sicher über Discord.
        </p>

        <?php if ($error): ?>
            <div class="error-box">
                <span>⚠</span>
                <span><?php echo htmlspecialchars($error); ?></span>
            </div>
        <?php endif; ?>

        <a href="<?php echo htmlspecialchars($discordUrl); ?>" class="btn-discord" id="discord-login-btn">
            <svg width="20" height="20" viewBox="0 0 127.14 96.36" fill="#fff" xmlns="http://www.w3.org/2000/svg">
                <path d="M107.7,8.07A105.15,105.15,0,0,0,81.47,0a72.06,72.06,0,0,0-3.36,6.83
                  A97.68,97.68,0,0,0,49,6.83,72.37,72.37,0,0,0,45.64,0,105.89,105.89,0,0,0,19.39,8.09
                  C2.79,32.65-1.71,56.6.54,80.21h0A105.73,105.73,0,0,0,32.71,96.36,77.7,77.7,0,0,0,39.6,85.25
                  a68.42,68.42,0,0,1-10.85-5.18c.91-.66,1.8-1.34,2.66-2a75.57,75.57,0,0,0,64.32,0c.87.71,1.76,1.39,2.66,2
                  a68.68,68.68,0,0,1-10.87,5.19,77,77,0,0,0,6.89,11.1A105.25,105.25,0,0,0,126.6,80.22h0
                  C129.24,52.84,122.09,29.11,107.7,8.07ZM42.45,65.69C36.18,65.69,31,60,31,53s5-12.74,11.43-12.74
                  S54,46,53.89,53,48.84,65.69,42.45,65.69Zm42.24,0C78.41,65.69,73.25,60,73.25,53s5-12.74,11.44-12.74
                  S96.23,46,96.12,53,91.08,65.69,84.69,65.69Z"/>
            </svg>
            Mit Discord anmelden
        </a>

        <p class="footer-note">
            Nur autorisierte Discord-Konten erhalten Zugang.<br>
            <a href="datenschutz.php">Datenschutzerklärung</a>
        </p>
    </div>
</body>
</html>
