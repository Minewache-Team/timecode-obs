<?php
/**
 * MW-Aufnahme System - Login
 *
 * Session-basierter Login fuer das Regisseur-Dashboard.
 */

session_start();
require_once __DIR__ . '/includes/config.php';

$error = '';

/* Logout */
if (isset($_GET['logout'])) {
    session_destroy();
    header('Location: login.php');
    exit;
}

/* Bereits eingeloggt? */
if (!empty($_SESSION['mw_authenticated'])) {
    header('Location: index.php');
    exit;
}

/* Login-Versuch */
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $user = trim($_POST['username'] ?? '');
    $pass = $_POST['password'] ?? '';

    if (hash_equals(DASHBOARD_USER, $user) && hash_equals(DASHBOARD_PASS, $pass)) {
        session_regenerate_id(true);
        $_SESSION['mw_authenticated'] = true;
        $_SESSION['mw_user'] = $user;
        header('Location: index.php');
        exit;
    } else {
        $error = 'Benutzername oder Passwort falsch.';
    }
}
?>
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MW Aufnahme - Login</title>
    <link rel="stylesheet" href="assets/style.css">
    <style>
        .login-wrapper {
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            padding: 20px;
        }
        .login-box {
            background: #16213e;
            border-radius: 12px;
            padding: 40px;
            width: 100%;
            max-width: 380px;
            box-shadow: 0 10px 40px rgba(0, 0, 0, 0.4);
        }
        .login-box h1 {
            color: #4fc3f7;
            font-size: 1.4rem;
            margin-bottom: 8px;
            text-align: center;
        }
        .login-box .subtitle {
            color: #888;
            font-size: 0.85rem;
            text-align: center;
            margin-bottom: 28px;
        }
        .login-box label {
            display: block;
            margin-bottom: 5px;
            font-size: 0.85rem;
            color: #aaa;
        }
        .login-box input[type="text"],
        .login-box input[type="password"] {
            width: 100%;
            padding: 10px 12px;
            margin-bottom: 16px;
            background: #0f3460;
            border: 1px solid #333;
            border-radius: 6px;
            color: #e0e0e0;
            font-size: 0.95rem;
        }
        .login-box input:focus {
            outline: none;
            border-color: #4fc3f7;
        }
        .login-box .btn-login {
            width: 100%;
            padding: 12px;
            background: #4fc3f7;
            color: #1a1a2e;
            border: none;
            border-radius: 6px;
            font-size: 1rem;
            font-weight: 700;
            cursor: pointer;
            transition: background 0.2s;
        }
        .login-box .btn-login:hover {
            background: #81d4fa;
        }
        .login-error {
            background: #3a1010;
            border: 1px solid #f44336;
            color: #f44336;
            padding: 10px 14px;
            border-radius: 6px;
            margin-bottom: 16px;
            font-size: 0.9rem;
            text-align: center;
        }
        .login-footer {
            margin-top: 20px;
            text-align: center;
        }
        .login-footer a {
            color: #888;
            font-size: 0.8rem;
            text-decoration: none;
        }
        .login-footer a:hover {
            color: #4fc3f7;
        }
    </style>
</head>
<body>
    <div class="login-wrapper">
        <div class="login-box">
            <h1>MW Aufnahme</h1>
            <p class="subtitle">Nur fuer den Regisseur</p>

            <?php if ($error): ?>
                <div class="login-error"><?= htmlspecialchars($error) ?></div>
            <?php endif; ?>

            <form method="POST" action="login.php">
                <label for="username">Benutzername</label>
                <input type="text" name="username" id="username" autocomplete="username" required autofocus>

                <label for="password">Passwort</label>
                <input type="password" name="password" id="password" autocomplete="current-password" required>

                <button type="submit" class="btn-login">Anmelden</button>
            </form>

            <div class="login-footer">
                <a href="datenschutz.php">Datenschutzerklaerung</a>
            </div>
        </div>
    </div>
</body>
</html>
