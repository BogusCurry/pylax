# database setup script, Thomas F�hringer, 2017-06-17

rm Hinterland.db
sqlite3 -init Hinterland.sql Hinterland.db
rm Backyard.db
sqlite3 -init Backyard.sql Backyard.db
