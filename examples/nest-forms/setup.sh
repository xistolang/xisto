#!/bin/sh
# Installs the packages this example uses from the repository's packages/ (they are not published yet).
cd "$(dirname "$0")" || exit 1
mkdir -p .xisto/pkgs/@nestjs
for p in class-validator class-transformer express rxjs nestjs-pino; do ln -sfn "../../../../packages/$p" ".xisto/pkgs/$p"; done
for p in common core config platform-express swagger; do ln -sfn "../../../../../packages/@nestjs/$p" ".xisto/pkgs/@nestjs/$p"; done
mkdir -p .xisto/pkgs/@elastic
ln -sfn "../../../../../packages/@elastic/ecs-pino-format" ".xisto/pkgs/@elastic/ecs-pino-format"
