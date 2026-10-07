# The front end (`./menu`)

Our own Megatouch menu, written from scratch (the cabinet's protected loader is never run).
It uses the cabinet's artwork from `menugraphics` (copied into `shared/data-common` by
`make setup`) and runs games through their normal `games/<name>/run`.

```
make            # builds shared/bin/megatouch-menu with the rest
./menu          # 1024x768 window; ./menu --window 1280x960 for another size
```

| Screen | What it does |
| --- | --- |
| Attract | Cycles the cabinet's attract pictures (`menugraphics/idle`). Touch to start. Back here after 1 minute idle. |
| Main | Six categories (cards, puzzles, quiz & word, sports, strategy, adult) with the classic category icons, ALL GAMES, HIGH SCORES. |
| Category | 12 game logos per page (`menugraphics/game/logos/english`), names from `config/gamedata.xml`. |
| Game info | Logo, name, best score, player count (p1–p4, up to the game's MaxPlayers), PLAY. |
| High scores | Each game's best score as megatouch-host saved it (`games/<g>/data/var/merit/highscores/<id>.txt`). |
| Operator (F2) | Free play on/off, credits, language (passed to games as MEGA_LANGUAGE), volume. Saved in `build/frontend.conf`. |

Keys: `C` adds a credit (coin), `F2` operator setup, `Esc` back / quit.

Catalogue: every `games/*/game.conf` with a `run` script. `FRONTEND_HIDE=1` hides services
(jukebox, operator setup). `CATEGORY_MENU=` overrides the category guessed from the name.

Headless check: `SDL_VIDEODRIVER=offscreen MEGA_MENU_SHOT=/abs/dir ./menu --window 640x480`
writes one screenshot per screen.

Not done yet: per-player names/high-score tables, the jukebox (ttunes), linked play, real coin
mechanics and accounting, volume actually applied to the games.
