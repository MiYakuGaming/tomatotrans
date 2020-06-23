.gba
.open "ta.gba","test.gba",0x08000000

.include "asm/addresses.inc"

; Game bugfix for saving in no$gba.  Possibly an emulator bug.
.include "asm/bug_nocash.asm"

; Required for the menu VWF fixes below to work.
.include "asm/fontmap.asm"
.include "asm/vwf_8x8.asm"

; The below lengthen text primarily in battle, and require battle_message.asm.
.include "asm/battle_message.asm"
.include "asm/battle_enemies.asm"
.include "asm/gimmick_text.asm"
.include "asm/item_text.asm"

; Allow longer text in the main battle menu.
.include "asm/battle_menu.asm"

; Add space for Strength in level up messages.
.include "asm/battle_levelup.asm"
; Line up better when gaining items from battle.
.include "asm/battle_spoils.asm"

; Updates for the menu options and descriptions.
.include "asm/menu.asm"
.include "asm/menu_gimmicks.asm"
.include "asm/menu_friends.asm"
.include "asm/shop_messages.asm"

; Allow longer item names and format as "#x Foo" for multiple.
.include "asm/dialog_item.asm"

; Use English uppercase/lowercase in the naming screen.
.include "asm/naming_screen.asm"
.include "asm/name_long.asm"

.close
