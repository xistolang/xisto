#!/bin/sh
# Empties the Conduit tables (the schema stays) in the test database container.
docker exec -i xisto-pg psql -q -U xisto -d conduit -c 'TRUNCATE "user", "article", "comment", "follows", "tag", "user_favorites_article" RESTART IDENTITY CASCADE;'
# and the cache (packages/redis or node-redis), so a run starts cold
docker exec xisto-redis redis-cli FLUSHALL > /dev/null 2>&1 || true
