#include "../include/types.h"
#include "../include/config.h"
#include "../include/constants/item.h"

/*
 *  Poké Mart stock.
 *
 *  The vanilla table sits in the arm9 at 0x020FBF22 and is hard capped at 19
 *  entries by the loop inside the mart_buy script command (0x02048060), with
 *  unrelated data directly behind it - so it cannot grow in place.  It is
 *  copied here instead and wired up by two patches:
 *
 *    repoints         arm9 sPokeMartItems 02048124
 *                     literal pool entry holding the table pointer
 *    bytereplacement  arm9 02048100 14
 *                     cmp r2, #19 -> cmp r2, #20, the entry count of the loop
 *                     that walks the table
 *
 *  When adding an entry here, bump that byte to the new entry count as well.
 *
 *  minBadgeTier is the badge tier needed before an item shows up in the shop:
 *  1 = no badges at all, 6 = eight badges.
 */
struct MartItem
{
    u16 item;
    u16 minBadgeTier;
};

struct MartItem sPokeMartItems[] =
{
    { ITEM_POKE_BALL,       1 },
    { ITEM_GREAT_BALL,      3 },
    { ITEM_ULTRA_BALL,      4 },
    { ITEM_POTION,          1 },
    { ITEM_SUPER_POTION,    2 },
    { ITEM_HYPER_POTION,    4 },
    { ITEM_MAX_POTION,      5 },
    { ITEM_FULL_RESTORE,    6 },
    { ITEM_REVIVE,          3 },
    { ITEM_ANTIDOTE,        1 },
    { ITEM_PARALYZE_HEAL,   1 },
    { ITEM_AWAKENING,       2 },
    { ITEM_BURN_HEAL,       2 },
    { ITEM_ICE_HEAL,        2 },
    { ITEM_FULL_HEAL,       4 },
    { ITEM_ESCAPE_ROPE,     2 },
    { ITEM_REPEL,           2 },
    { ITEM_SUPER_REPEL,     3 },
    { ITEM_MAX_REPEL,       4 },
    { ITEM_ENCOUNTER_LURE,  1 },
    { ITEM_EXP_CANDY_XS,    1 },
    { ITEM_EXP_CANDY_S,     1 },
    { ITEM_EXP_CANDY_M,     1 },
    { ITEM_EXP_CANDY_L,     1 },
    { ITEM_EXP_CANDY_XL,    1 },
    // TODO: temporary, only here so the box link can be obtained for testing.
    // remove again once it has a proper source and bump the count in bytereplacement
    { ITEM_POKEMON_BOX_LINK, 1 },
};
