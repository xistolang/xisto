#!/bin/sh
# Installs the packages this example uses from the repository's packages/ (they are not published yet).
cd "$(dirname "$0")" || exit 1
mkdir -p .xisto/pkgs/@nestjs
for p in express rxjs passport-jwt jwks-rsa class-validator class-transformer; do ln -sfn "../../../../packages/$p" ".xisto/pkgs/$p"; done
for p in common core config passport throttler; do ln -sfn "../../../../../packages/@nestjs/$p" ".xisto/pkgs/@nestjs/$p"; done
