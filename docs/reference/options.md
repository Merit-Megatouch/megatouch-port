# Game options

The loader's 120 game options (`xml_gameoptions::GameOptionIndex`, names from the cabinet's
`libenums.so`, `_INX` suffix dropped). Each is one byte in `/var/merit/nvram.dat` at
`0x40 + index`, 0 or 1. The security key licenses each one (0 locked off, 1 locked on, 2/3
operator's choice with default off/on); Operator Setup only shows the operator's choices.
Details: [cabinet software → NVRAM](cabinet-software.md#nvram-and-game-options).

- **Image value:** the value on the disk image this project was built from, as shipped. Other
  cabinets differ.
- **Our key:** what `scripts/loader-key.sh` licenses. Everything is the operator's choice except
  the placeholders and options for hardware a PC doesn't have, which are locked off.

Read or change them with `scripts/loader-option.sh` (cabinet stopped) or in Operator Setup.
Regenerate this table from a cabinet's state with
`scripts/loader-option.sh --list`.

| Index | Name | Image value | Our key |
| --- | --- | --- | --- |
| 0 | `SOL_FREE_GAME` | 0 | choice |
| 1 | `ALLOW_GAME_CONTINUES` | 1 | choice |
| 2 | `ENABLE_RENTAL_MODE` | 0 | choice |
| 3 | `PSYCHOGRAPHIC_PROFILES_ENABLED` | 0 | choice |
| 4 | `BONUS_REPLAY_ALLOWED` | 0 | choice |
| 5 | `PRICE_IN_CRED_OR_MONEY` | 1 | choice |
| 6 | `SORT_PRICE_DESCRIPTIONS` | 0 | choice |
| 7 | `POWSOL_SPLIT_COL` | 1 | choice |
| 8 | `MASTER_SEX_KILL` | 0 | choice |
| 9 | `TRIVIA_SHOW_ANSWER` | 0 | choice |
| 10 | `TRIVIA_NUM_PRIM_RNDS` | 1 | choice |
| 11 | `TMAXX_OK_IN_FREEPLAY` | 0 | choice |
| 12 | `PRICE_DIFF_THAN_TMAXX` | 0 | choice |
| 13 | `SIX_STARS_ENABLED` | 1 | choice |
| 14 | `POWSOL_FREE_GAME` | 0 | choice |
| 15 | `RENTAL_MODE_5_ROW_SUPPORT` | 1 | choice |
| 16 | `TOUCH_2_PLAY_MODE_ENABLED` | 0 | choice |
| 17 | `GOLF_HAS_WOMEN` | 1 | choice |
| 18 | `PHUNT_NEW_VER` | 1 | choice |
| 19 | `MERIT_MONEY_ENABLED` | 1 | choice |
| 20 | `TOURNAMAXX_ENABLED` | 0 | choice |
| 21 | `MUSIC_TIMER_FROM_6STARS` | 1 | choice |
| 22 | `PLR_SELECT_LANG` | 1 | choice |
| 23 | `AUTO_CLR_HISCR` | 0 | choice |
| 24 | `ALLOW_CLR_CREDS` | 1 | choice |
| 25 | `STRIP_MOANS` | 0 | choice |
| 26 | `HISCORE_FROM_6STARS` | 0 | choice |
| 27 | `VBBOARD_FROM_6STARS` | 1 | choice |
| 28 | `VOLUME_FROM_6STARS` | 1 | choice |
| 29 | `DISABLE_FREE_CREDS` | 0 | choice |
| 30 | `ALLOW_NUDITY` | 1 | choice |
| 31 | `NUDITY_LEVEL` | 1 | choice |
| 32 | `SHOW_DECK` | 0 | choice |
| 33 | `CHECKERS_RULES` | 0 | choice |
| 34 | `DECK_FAN_TIME` | 1 | choice |
| 35 | `LANG_BUT_FLAGS` | 1 | choice |
| 36 | `TOURN_SUPPORT_INX_PRIVATE` | 1 | choice |
| 37 | `CAL_FROM_6STARS` | 1 | choice |
| 38 | `TRIT_FACE_UP` | 1 | choice |
| 39 | `STRIP_REWIND` | 0 | choice |
| 40 | `ENABLE_KIDS_CATEGORY` | 1 | choice |
| 41 | `FREE_PLAY_ENABLED` | 1 | choice |
| 42 | `GAMES_EASY` | 0 | choice |
| 43 | `NEWTRIVIA_SAME_CAT` | 0 | choice |
| 44 | `NEWTRIVIA_EXTRA_QUES` | 1 | choice |
| 45 | `MY_MERIT_ENABLED` | 1 | choice |
| 46 | `CONTINUOUS_BONUS_ROUND` | 1 | choice |
| 47 | `SENIOR_MODE_ENABLED` | 0 | choice |
| 48 | `ENABLE_THEFT_DETERRENT` | 0 | choice |
| 49 | `LEASE_MODE_ENABLED` | 0 | choice |
| 50 | `TMAXX_FROM_6STARS` | 0 | choice |
| 51 | `PROMO_CREDITS_ENABLED` | 1 | choice |
| 52 | `LINKED_GAMES_ENABLED` | 0 | choice |
| 53 | `NO_NAMES_IN_HI_SCORES` | 0 | choice |
| 54 | `ENABLE_WEB_FILTER` | 1 | choice |
| 55 | `ENABLE_HIGH_RES` | 1 | choice |
| 56 | `ENABLE_MEGAWEB` | 0 | choice |
| 57 | `ENABLE_PRIZEZONE` | 0 | choice |
| 58 | `ENABLE_PREMIUM_EROTIC` | 0 | choice |
| 59 | `ENABLE_ENT_CHANNEL_REMOVED` | 1 | choice |
| 60 | `ENABLE_ED_ATTRACT` | 1 | choice |
| 61 | `ENABLE_COINLESS_COINOP` | 0 | choice |
| 62 | `ENABLE_DDC` | 1 | choice |
| 63 | `ENABLE_QUICK_PRICING` | 0 | choice |
| 64 | `ENABLE_PREM_ERO_IDLE_AD_INS` | 1 | choice |
| 65 | `ENABLE_SOUND_IN_IDLE_MODE` | 0 | choice |
| 66 | `ENABLE_OPERATOR_WEBSITE` | 0 | choice |
| 67 | `ENABLE_MINDSPARK_MODE` | 0 | locked off (MindSpark mode: changes platform detection) |
| 68 | `ENABLE_SHOWTENDERS_DEPRECATED` | 0 | choice |
| 69 | `ENABLE_AUTO_CONTINUE` | 0 | choice |
| 70 | `ENABLE_CARD_BACK_AD_SUPPORT` | 1 | choice |
| 71 | `ENABLE_PREMIERE_TOURN_TERMINAL` | 0 | choice |
| 72 | `STICKER_BOOK_TIMER` | 0 | choice |
| 73 | `PLAYER_KEY_ENABLED` | 0 | choice |
| 74 | `ENABLE_BOWLING_POKER` | 0 | choice |
| 75 | `SLOW_SENIOR_GAMES` | 0 | choice |
| 76 | `ENABLE_HI_RES_FOR_TT` | 0 | choice |
| 77 | `ENABLE_EROTIC_IN_ATTRACT_MODE` | 0 | choice |
| 78 | `ENABLE_EROTIC_IN_NEW_GAMES_CAT` | 0 | choice |
| 79 | `AUTO_PLACE_EROTIC_IN_NEW_GAMES_CAT` | 0 | choice |
| 80 | `NETWORK_OPTIONS_FROM_6STARS` | 0 | choice |
| 81 | `ALT_LANGUAGE_SELECT_ON` | 1 | choice |
| 82 | `ACCESS_OPSETUP_FROM_SIXSTARS` | 0 | choice |
| 83 | `ACCESS_UNLOCK_FIREFLY_FROM_SIXSTARS` | 0 | choice |
| 84 | `ALLOW_PLAYER_UNLOCK_FIREFLY` | 0 | choice |
| 85 | `PRIZE_POOL_ON_MAIN_MENU` | 0 | choice |
| 86 | `ACCESS_LOCATION_CARD_SETUP_FROM_SIXSTARS` | 0 | choice |
| 87 | `UNUSED_OPTION5_DEFYES` | 1 | choice |
| 88 | `TOUCHTUNES_ENABLED` | 0 | locked off (TouchTunes) |
| 89 | `GENERIC_KIDZPACE_MODE_ENABLED` | 0 | choice |
| 90 | `ACCESS_FREE_CREDITS_FROM_SIXSTARS` | 0 | choice |
| 91 | `BYPASS_WIRELESS_SECURITY` | 0 | choice |
| 92 | `ALLOW_COIN_IN_TABLE_MODIFICATION` | 1 | choice |
| 93 | `UAE_PRIZE_MODE_ENABLED` | 0 | choice |
| 94 | `UNUSED_OPTION3_DEFNO` | 0 | choice |
| 95 | `UNUSED_OPTION4_DEFNO` | 0 | choice |
| 96 | `DISABLE_QUESTIONABLE_CONTENT` | 0 | choice |
| 97 | `TURN_OFF_AUTOMATIC_RECONNECT` | 1 | choice |
| 98 | `DISABLE_AMI_EXIT` | 0 | choice |
| 99 | `ENABLE_AMI` | 1 | choice |
| 100 | `CREDITCARD_ENABLED` | 0 | locked off (credit card) |
| 101 | `CREDITCARD_PULSE_HARD_METER` | 0 | locked off (credit card) |
| 102 | `UNUSED_OPTION5_DEFNO` | 0 | choice |
| 103 | `FIREFLY_MODE_ENABLED` | 0 | choice |
| 104 | `UNUSED_OPTION2_ALWAYS` | 1 | locked on (placeholder) |
| 105 | `UNUSED_OPTION3_ALWAYS` | 1 | locked on (placeholder) |
| 106 | `UNUSED_OPTION4_ALWAYS` | 1 | locked on (placeholder) |
| 107 | `UNUSED_OPTION5_ALWAYS` | 1 | locked on (placeholder) |
| 108 | `UNUSED_OPTION6_ALWAYS` | 1 | locked on (placeholder) |
| 109 | `ALLOW_MEGANET_WITHOUT_TOURNAMAXX` | 0 | choice |
| 110 | `UNUSED_OPTION1_NEVER` | 0 | locked off (placeholder) |
| 111 | `UNUSED_OPTION2_NEVER` | 0 | locked off (placeholder) |
| 112 | `UNUSED_OPTION3_NEVER` | 0 | locked off (placeholder) |
| 113 | `UNUSED_OPTION4_NEVER` | 0 | locked off (placeholder) |
| 114 | `ENABLE_DOWNLOAD_SELECTOR` | 0 | locked off (Rowe download selector: needs its board, else "invalid key with the current hardware configuration") |
| 115 | `CREDITCARD_DEMO_MODE` | 0 | locked off (credit card) |
| 116 | `ENABLE_SHOWTENDERS` | 0 | choice |
| 117 | `ENABLE_REV_SHARE` | 0 | choice |
| 118 | `NETWORK_ACCESS_FROM_SIX_STARS` | 0 | choice |
| 119 | `UNUSED_OPTION6_NEVER` | 0 | locked off (placeholder) |
