#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
prefix="${DEEPSEEK_INSTALL_DIR:-$HOME/.local/share/deepseek-chat}"
if [[ "$prefix" != /* || "$prefix" == *$'\n'* || "$prefix" == *'"'* || "$prefix" == *'`'* || "$prefix" == *'$'* || "$prefix" == *'\\'* ]]; then
    echo 'Use um caminho absoluto simples em DEEPSEEK_INSTALL_DIR.' >&2
    exit 1
fi
missing="$(ldd ./deepseek-chat | awk '/not found/{print $1}')"
if [[ -n "$missing" ]]; then
    printf 'Bibliotecas ausentes:\n%s\nConsulte LEIA-ME.txt para instalar as dependências.\n' "$missing" >&2
    exit 1
fi
mkdir -p "$prefix" "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/256x256/apps"
temporary="$(mktemp "$prefix/.deepseek-chat-update.XXXXXX")"
trap 'if [[ -e "$temporary" ]]; then unlink "$temporary"; fi' EXIT
install -m755 deepseek-chat "$temporary"
mv -f "$temporary" "$prefix/deepseek-chat"
trap - EXIT
install -m644 LEIA-ME.txt "$prefix/LEIA-ME.txt"
if [[ ! -e "$prefix/config.ini" ]]; then
    install -m600 config.ini.example "$prefix/config.ini"
fi
install -m644 deepseek-chat.png "$HOME/.local/share/icons/hicolor/256x256/apps/deepseek-chat.png"
cat > "$HOME/.local/bin/deepseek-chat" <<EOF
#!/usr/bin/env bash
cd "$prefix"
exec "$prefix/deepseek-chat" "\$@"
EOF
chmod 755 "$HOME/.local/bin/deepseek-chat"
cat > "$HOME/.local/share/applications/deepseek-chat.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=DeepSeek Chat
Comment=Cliente desktop de inteligência artificial
Exec="$prefix/deepseek-chat"
Path=$prefix
Icon=deepseek-chat
Terminal=false
Categories=Network;Office;
EOF
command -v update-desktop-database >/dev/null && update-desktop-database "$HOME/.local/share/applications" || true
printf 'Instalado em %s\nAbra DeepSeek Chat pelo menu de aplicativos.\n' "$prefix"
