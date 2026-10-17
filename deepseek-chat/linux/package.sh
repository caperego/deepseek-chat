#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
[[ "$(uname -s)" == Linux ]] || { echo 'Execute no Linux.' >&2; exit 1; }
if [[ "${1:-}" != --no-build ]]; then bash "$root/build.sh"; fi
version="$(sed -n 's/.*kAppVersion = "\([^"]*\)";.*/\1/p' "$root/src/appcontroller.cpp")"
name="deepseek-chat-${version}-linux-$(uname -m)"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$root/dist" "$stage/$name"
install -m755 "$root/bin/deepseek-chat" "$stage/$name/deepseek-chat"
install -m755 "$root/linux/install.sh" "$stage/$name/install.sh"
install -m644 "$root/resources/deepseek-256.png" "$stage/$name/deepseek-chat.png"
install -m644 "$root/../LICENSE" "$stage/$name/LICENSE"
# Nunca distribuir configurações locais, tokens ou credenciais.
printf '[api]\nbase_url=https://SEU_DOMINIO/deepseek\n\n[ui]\ntheme=system\nclose_to_tray=true\n' > "$stage/$name/config.ini.example"
cat > "$stage/$name/LEIA-ME.txt" <<EOF
DeepSeek Chat $version — Linux $(uname -m)

Compilado com $(qmake6 -query QT_VERSION), no Manjaro/Arch Linux.
Este pacote depende das bibliotecas do sistema; não é um AppImage.
Requer Linux com glibc e Qt compatíveis com a máquina de compilação.

Dependências no Manjaro/Arch:
sudo pacman -S --needed qt6-base qt6-declarative qt6-multimedia qt6-speech qt6-svg qt6-webengine

Instalar: bash install.sh
O instalador .run instala diretamente, sem extração manual.
Destino padrão: ~/.local/share/deepseek-chat
Atalho de terminal: ~/.local/bin/deepseek-chat
Atalho no menu: DeepSeek Chat
Configuração: ~/.local/share/deepseek-chat/config.ini
Configure a URL do seu backend e faça login no aplicativo.
Atualizações pelo instalador preservam config.ini e mcp.json.

Desinstalar (preserva configuração e dados):
rm -f ~/.local/share/deepseek-chat/deepseek-chat ~/.local/bin/deepseek-chat
rm -f ~/.local/share/applications/deepseek-chat.desktop
rm -f ~/.local/share/icons/hicolor/256x256/apps/deepseek-chat.png

Outras distribuições: compile com o Qt da própria distribuição usando build.sh.
EOF
tar -C "$stage" -czf "$root/dist/$name.tar.gz" "$name"
installer="$root/dist/$name.run"
cat > "$installer" <<'HEADER'
#!/usr/bin/env bash
set -euo pipefail
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
line="$(awk '/^__DEEPSEEK_ARCHIVE_BELOW__$/{print NR+1;exit}' "$0")"
tail -n +"$line" "$0" | tar -xz -C "$tmp"
bash "$tmp"/*/install.sh
exit 0
__DEEPSEEK_ARCHIVE_BELOW__
HEADER
cat "$root/dist/$name.tar.gz" >> "$installer"
chmod 755 "$installer"
(cd "$root/dist" && sha256sum "$name.run" "$name.tar.gz" > "$name.sha256")
printf 'Pacotes gerados: %s/dist/%s.{run,tar.gz,sha256}\n' "$root" "$name"
