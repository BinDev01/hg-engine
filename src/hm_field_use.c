#include "../include/types.h"
#include "../include/config.h"
#include "../include/bag.h"
#include "../include/pokemon.h"
#include "../include/save.h"
#include "../include/script.h"
#include "../include/constants/item.h"
#include "../include/constants/moves.h"

#ifdef HMS_USABLE_FROM_BAG

/*
 *  Lets the HM field moves be used without any party Pokémon having learned them.
 *
 *  Vanilla asks "who in the party can do this?" in exactly two places, and both are
 *  replaced wholesale through the hooks file:
 *
 *    0x0204D3CC  ScrCmd_GetPartySlotWithMove - script command 141.  Used by the Cut,
 *                Rock Smash, Strength, Whirlpool and Rock Climb map scripts.  Writes
 *                the party slot into a script variable, or 6 when nobody qualifies.
 *    0x020542E8  GetPartySlotWithMove - returns 0xFF when nobody qualifies.  Has two
 *                callers, both of them in overlay 1: 0x021E6BBC (Waterfall, move 127)
 *                and 0x021E7542 (Surf, move 57).  Those are the two moves that are
 *                triggered by walking into a tile instead of by a map script.
 *
 *  Both keep the original party scan and only fall back to the bag when it turns up
 *  nothing, so a Pokémon that really knows the move is still preferred and still gets
 *  named in the "used Cut!" message.
 *
 *  Badge requirements are untouched - the map scripts run check_badge separately, and
 *  that command is not involved here.
 *
 *  Fly is deliberately not covered.  It is only ever used from the party menu, which
 *  walks the field move table at 0x021013C4 and never reaches either function.
 */

#define PARTY_SLOT_NONE          0xFF // what GetPartySlotWithMove returns for "nobody"
#define PARTY_SLOT_NONE_SCRIPT      6 // what script command 141 writes for "nobody"

// HM01..HM08 in item order, mirroring the vanilla table at 0x02100184
static const u16 sHiddenMachineMoves[NUM_HMS] = {
    MOVE_CUT,         // HM01
    MOVE_FLY,         // HM02
    MOVE_SURF,        // HM03
    MOVE_STRENGTH,    // HM04
    MOVE_WHIRLPOOL,   // HM05
    MOVE_ROCK_SMASH,  // HM06
    MOVE_WATERFALL,   // HM07
    MOVE_ROCK_CLIMB,  // HM08
};

static u16 MoveToHiddenMachine(u16 move)
{
    u32 i;

    for (i = 0; i < NUM_HMS; i++)
    {
        if (sHiddenMachineMoves[i] == move)
        {
            return ITEM_HM01 + i;
        }
    }
    return ITEM_NONE;
}

/*
 *  HMs always sit in the TM/HM pocket, so that pocket is scanned directly instead of
 *  going through Bag_HasItem.  That one calls Bag_GetItemPocket, which loads item data
 *  off the heap, and this runs on every step towards water.
 */
static BOOL BagHasHiddenMachineForMove(u16 move)
{
    BAG_DATA *bag;
    u32 i;
    u16 item = MoveToHiddenMachine(move);

    if (item == ITEM_NONE)
    {
        return FALSE;
    }

    bag = Sav2_Bag_get(SaveBlock2_get());
    if (bag == NULL)
    {
        return FALSE;
    }

    for (i = 0; i < NUM_BAG_TMS_HMS; i++)
    {
        if (bag->TMsHMs[i].id == item && bag->TMsHMs[i].quantity != 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static u8 FirstPartySlotThatIsNotAnEgg(struct Party *party)
{
    u8 count = PokeParty_GetPokeCount(party);
    u8 i;

    for (i = 0; i < count; i++)
    {
        if (GetMonData(Party_GetMonByIndex(party, i), MON_DATA_IS_EGG, NULL) == 0)
        {
            return i;
        }
    }
    return PARTY_SLOT_NONE;
}

static u8 FindPartySlotThatKnowsMove(struct Party *party, u16 move)
{
    u8 count = PokeParty_GetPokeCount(party);
    u8 i;
    u32 j;

    for (i = 0; i < count; i++)
    {
        struct PartyPokemon *mon = Party_GetMonByIndex(party, i);

        if (GetMonData(mon, MON_DATA_IS_EGG, NULL) != 0)
        {
            continue;
        }

        for (j = 0; j < 4; j++)
        {
            if (GetMonData(mon, MON_DATA_MOVE1 + j, NULL) == move)
            {
                return i;
            }
        }
    }
    return PARTY_SLOT_NONE;
}

/**
 *  @brief full replacement of the vanilla routine at 0x020542E8
 *
 *  @param party the player's party
 *  @param move the field move that wants to be used
 *  @return party slot that gets to use it, PARTY_SLOT_NONE if none can
 */
u8 GetPartySlotWithMove(struct Party *party, u16 move)
{
    u8 slot = FindPartySlotThatKnowsMove(party, move);

    if (slot != PARTY_SLOT_NONE)
    {
        return slot;
    }

    if (BagHasHiddenMachineForMove(move))
    {
        return FirstPartySlotThatIsNotAnEgg(party);
    }

    return PARTY_SLOT_NONE;
}

/**
 *  @brief full replacement of script command 141 at 0x0204D3CC
 *
 *  @param ctx script context structure
 *  @return FALSE
 */
BOOL ScrCmd_GetPartySlotWithMove(SCRIPTCONTEXT *ctx)
{
    FieldSystem *fsys = ctx->fsys;
    u16 *dest;
    u16 move;
    u8 slot;

    // argument order matters, vanilla reads the destination variable before the move
    dest = ScriptGetVarPointer(ctx);
    move = ScriptGetVar(ctx);

    slot = GetPartySlotWithMove(SaveData_GetPlayerPartyPtr(fsys->savedata), move);
    *dest = (slot == PARTY_SLOT_NONE) ? PARTY_SLOT_NONE_SCRIPT : slot;

    return FALSE;
}

#endif // HMS_USABLE_FROM_BAG
