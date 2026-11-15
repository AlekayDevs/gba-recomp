#!/bin/sh
# Instala el hook pre-push: ejecuta tools/scan.py --history y cancela el push si encuentra algo de la ROM.
set -e
H=$(git rev-parse --git-dir)/hooks/pre-push
cat > "$H" <<'EOF'
#!/bin/sh
exec python -I "$(git rev-parse --show-toplevel)/tools/scan.py" --history
EOF
chmod +x "$H"
echo "hook instalado: $H"
