#!/bin/sh
# Installs the packages this example uses from the repository's packages/ (they are not published yet).
cd "$(dirname "$0")" || exit 1
mkdir -p .xisto/pkgs/@nestjs
for p in pg jsonwebtoken argon2 slug class-validator class-transformer express redis; do ln -sfn "../../../../packages/$p" ".xisto/pkgs/$p"; done
for p in common core swagger; do ln -sfn "../../../../../packages/@nestjs/$p" ".xisto/pkgs/@nestjs/$p"; done
