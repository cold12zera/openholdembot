//******************************************************************************
//
// This file is part of the OpenHoldem project
//   Download page:         http://code.google.com/p/openholdembot/
//   Forums:                http://www.maxinmontreal.com/forums/index.php
//   Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose:
//
//******************************************************************************

#include "stdafx.h"
#include "CSymbolEngineVersus.h"

#include <io.h>
#include <fcntl.h>
#include "CardFunctions.h"
#include "CBetroundCalculator.h"
#include "inlines/eval.h"
#include "CFilenames.h"
#include "CFunctionCollection.h"
#include "CPreferences.h"
#include "CScraper.h"
#include "CScraperAccess.h"
#include "CSymbolEngineUserchair.h"
#include "CTableState.h"
#include "OH_MessageBox.h"
#include "COHScriptList.h"

CSymbolEngineVersus *p_symbol_engine_versus = NULL;

CSymbolEngineVersus::CSymbolEngineVersus() {
  // The values of some symbol-engines depend on other engines.
	// As the engines get later called in the order of initialization
	// we assure correct ordering by checking if they are initialized.
  assert(p_symbol_engine_userchair != NULL);
  // Check versus.bin
  _sopen_s(&_versus_fh, p_filenames->VersusPath(), _O_RDONLY | _O_BINARY, _SH_DENYWR, NULL);
	if (_versus_fh == k_undefined) {
		// We do no longer warn directly, 
		// but only when versus symbols get used without the file.
		_versus_bin_not_loaded = true;
	}	else {
		_versus_bin_not_loaded = false;
	}
}

CSymbolEngineVersus::~CSymbolEngineVersus() {
}

void CSymbolEngineVersus::InitOnStartup() {
}

void CSymbolEngineVersus::ResetOnConnection() {
}

void CSymbolEngineVersus::ResetOnHandreset() {
}

void CSymbolEngineVersus::ResetOnNewRound() {
}

void CSymbolEngineVersus::ResetOnMyTurn() {
}

void CSymbolEngineVersus::ResetOnHeartbeat() {
  GetCounts();
}

void CSymbolEngineVersus::DoCalc(const CardMask plCards, const CardMask oppCards, const CardMask comCards,
					 unsigned int *wintemp, unsigned int *tietemp, unsigned int *lostemp) 
{
	CardMask		playerEvalCards, oppEvalCards;
	HandVal			player_hv = 0, opp_hv = 0;

	CardMask_OR(playerEvalCards, plCards, comCards);
	CardMask_OR(oppEvalCards, oppCards, comCards);

	player_hv = Hand_EVAL_N(playerEvalCards, 7);
	opp_hv = Hand_EVAL_N(oppEvalCards, 7);

	if (player_hv > opp_hv)
		*wintemp = *wintemp + 1;

	else if (player_hv < opp_hv)
		*lostemp = *lostemp + 1;

	else
		*tietemp = *tietemp + 1;
}

bool CSymbolEngineVersus::CheckForLoadedVersusBin() {
	if (_versus_bin_not_loaded)	{
		OH_MessageBox_Error_Warning("Impossible to use versus-symbols.\n"
			"Versus.bin not loaded and probably not installed.\n"
			"Please download this file from our download page:\n"
      "https://sites.google.com/site/openholdempokerbot/downloads", 
			"Versus Error"); 
    return false;
	}
  return true;
}

void CSymbolEngineVersus::ClearWinTieLosData() {
  for (int i=0; i<(k_number_of_cards_per_deck - 1); i++) 	{
	  for (int j=i+1; j<k_number_of_cards_per_deck; j++) {
      _n_win_against_hand[i][j] = 0;
      _n_tie_against_hand[i][j] = 0;
      _n_los_against_hand[i][j] = 0;
    }
  }
}

bool CSymbolEngineVersus::GetCounts() {
	if (_versus_fh == k_undefined) return false;

	CardMask		plCards, oppCards, deadCards, comCardsScrape, comCardsEnum, comCardsAll, usedCards;
	unsigned int	wintemp = 0, tietemp = 0, lostemp = 0;
	unsigned int	nhiwin = 0, nhitie = 0, nhilos = 0, ntiwin = 0, ntitie = 0, ntilos = 0, nlowin = 0, nlotie = 0, nlolos = 0;
	unsigned int	nhinowwin = 0, nhinowtie = 0, nhinowlos = 0, ntinowwin = 0, ntinowtie = 0, ntinowlos = 0, nlonowwin = 0, nlonowtie = 0, nlonowlos = 0;
  unsigned int	c0rank = 0, c1rank = 0, temprank = 0;
	BYTE			byte[8] = {0};
	long			pos = 0;
	int				listnum = 0;
	
	int betround = p_betround_calculator->betround();
	int sym_userchair = p_symbol_engine_userchair->userchair();

	for (int i=0; i<k_number_of_cards_per_player; i++) {
    card_player[i] = p_table_state->_players[sym_userchair]._hole_cards[i].GetValue();
  }
	for (int i=0; i<k_number_of_community_cards; i++) {
    card_common[i] = p_table_state->_common_cards[i].GetValue();
  }
  // Get the lock
	CSLock lock(m_critsec);
  ClearWinTieLosData();
	if (!p_symbol_engine_userchair->userchair_confirmed()) return false;

  if (!p_table_state->User()->HasKnownCards()) return false;

	_nwin = _ntie = _nlos = _nhands = 0;
	_nhandshi = _nhandsti = _nhandslo = 0;
	_nhandshinow = _nhandstinow = _nhandslonow = 0;
	_vsprwinhi = _vsprtiehi = _vsprloshi = 0;
	_vsprwinti = _vsprtieti = _vsprlosti = 0;
	_vsprwinlo = _vsprtielo = _vsprloslo = 0;
	_vsprwinhinow = _vsprtiehinow = _vsprloshinow = 0;
	_vsprwintinow = _vsprtietinow = _vsprlostinow = 0;
	_vsprwinlonow = _vsprtielonow = _vsprloslonow = 0;
	nhiwin = nhitie = nhilos = ntiwin = ntitie = ntilos = nlowin = nlotie = nlolos = 0;

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// PREFLOP
	if (betround == k_betround_preflop) {
    if (card_player[0] >= card_player[1]) {
		  SwapInts(&card_player[0], &card_player[1]);
    }
	  // figure out offset into file
	  unsigned int offset = 0;
	  //for (int i=1; i<card_player[0]; i++)  offset += (52-i)*1225;
	  offset += card0_offset[card_player[0]];
	  offset += (card_player[1]-card_player[0]-1)*1225;
	  offset *= sizeof(byte);

	  // seek to right position in file
	  if ((pos = _lseek(_versus_fh, offset, SEEK_SET)) == long(k_undefined)) {
		  return false;
	  }

	  wintemp = lostemp = 0;
	  for (int i=0; i<(k_number_of_cards_per_deck - 1); i++) 	{
		  for (int j=i+1; j<k_number_of_cards_per_deck; j++) {
			  if (i!=card_player[0] && i!=card_player[1] && j!=card_player[0] && j!=card_player[1]) {
				  _read(_versus_fh, &byte, sizeof(byte));
				  memcpy(&wintemp, &byte[0], sizeof(unsigned int));
				  memcpy(&lostemp, &byte[4], sizeof(unsigned int));

				  _nwin += wintemp;
				  _ntie += 1712304 - wintemp - lostemp;
				  _nlos += lostemp;
				  _nhands = _nhands + 1;

				  if (wintemp<lostemp) {
					  _nhandshi = _nhandshi + 1;
					  nhiwin += wintemp;
					  nhitie += 1712304 - wintemp - lostemp;
					  nhilos += lostemp;
				  } else if (wintemp>lostemp) {
					  _nhandslo = _nhandslo + 1;
					  nlowin += wintemp;
					  nlotie += 1712304 - wintemp - lostemp;
					  nlolos += lostemp;
				  } else {
					  _nhandsti = _nhandsti + 1;
					  ntiwin += wintemp;
					  ntitie += 1712304 - wintemp - lostemp;
					  ntilos += lostemp;
				  }

				  // Calculations for vs$xx$prwin, vs$xx$prtie, vs$xx$prlos
				  c0rank = StdDeck_RANK(i);
				  c1rank = StdDeck_RANK(j);
				  if (c0rank < c1rank) {
					  temprank = c0rank;
					  c0rank = c1rank;
					  c1rank = temprank;
				  }
				  _n_win_against_hand[i][j] = wintemp;
          _n_tie_against_hand[i][j] = tietemp;
          _n_los_against_hand[i][j] = lostemp;
			  }
		  }
	  }		
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// FLOP, TURN, RIVER
	else if (betround >= k_betround_flop) 
	{
      CardMask		playerEvalCardsNow, oppEvalCardsNow;
	   HandVal		player_hv_now = 0, opp_hv_now = 0; 
         
		// Common cards
		CardMask_RESET(comCardsScrape);
		if (betround >= k_betround_flop)  CardMask_SET(comCardsScrape, card_common[0]);
		if (betround >= k_betround_flop)  CardMask_SET(comCardsScrape, card_common[1]);
		if (betround >= k_betround_flop)  CardMask_SET(comCardsScrape, card_common[2]);
		if (betround >= k_betround_turn)  CardMask_SET(comCardsScrape, card_common[3]);
		if (betround >= k_betround_river) CardMask_SET(comCardsScrape, card_common[4]);

		// player cards
		CardMask_RESET(plCards);
		CardMask_SET(plCards, card_player[0]);
		CardMask_SET(plCards, card_player[1]);

		// all used cards
		CardMask_OR(usedCards, comCardsScrape, plCards);

		// eval player hand now
		CardMask_OR(playerEvalCardsNow, plCards, comCardsScrape);
		player_hv_now = Hand_EVAL_N(playerEvalCardsNow, betround+3);
	   
		// Enumerate through all possible opponent hands (excludes already used cards)
		for (int i=0; i<(k_number_of_cards_per_deck-1); i++)
		{
			for (int j=i+1; j<k_number_of_cards_per_deck; j++)
			{
				if (!CardMask_CARD_IS_SET(usedCards, i) && !CardMask_CARD_IS_SET(usedCards, j))
				{
					CardMask_RESET(oppCards);
					CardMask_SET(oppCards, i);
					CardMask_SET(oppCards, j);

					// Enumerate through all possible river situations (exclude player cards and opponent cards)
					CardMask_OR(deadCards, usedCards, oppCards);
					wintemp = tietemp = lostemp = 0;

					if (betround==k_betround_flop || betround==k_betround_turn)
					{
						ENUMERATE_N_CARDS_D(comCardsEnum, betround==k_betround_flop ? 2 : 
							betround==k_betround_turn ? 1 : 0, deadCards,
						{
							CardMask_OR(comCardsAll, comCardsScrape, comCardsEnum);
							DoCalc(plCards, oppCards, comCardsAll, &wintemp, &tietemp, &lostemp);
						});
					}
					else
					{
						DoCalc(plCards, oppCards, comCardsScrape, &wintemp, &tietemp, &lostemp);
					}

					_nwin += wintemp;
					_ntie += tietemp;
					_nlos += lostemp;
					_nhands = _nhands + 1;

					if (wintemp<lostemp)
					{
						_nhandshi = _nhandshi + 1;
						nhiwin += wintemp;
						nhitie += tietemp;
						nhilos += lostemp;
					}
					else if (wintemp>lostemp)
					{
						_nhandslo = _nhandslo + 1;
						nlowin += wintemp;
						nlotie += tietemp;
						nlolos += lostemp;
					}
					else
					{
						_nhandsti = _nhandsti + 1;
						ntiwin += wintemp;
						ntitie += tietemp;
						ntilos += lostemp;
					}
               
               //eval opponent cards now
               CardMask_OR(oppEvalCardsNow, oppCards, comCardsScrape);
	           opp_hv_now  = Hand_EVAL_N(oppEvalCardsNow, betround+3);

	            if (player_hv_now < opp_hv_now)
               {
						_nhandshinow = _nhandshinow + 1;
						nhinowwin += wintemp;
						nhinowtie += tietemp;
						nhinowlos += lostemp;
               }
	            else if (player_hv_now  > opp_hv_now)
               {
               	_nhandslonow = _nhandslonow + 1;
						nlonowwin += wintemp;
						nlonowtie += tietemp;
						nlonowlos += lostemp;
					}
	        else {
              _nhandstinow = _nhandstinow + 1;
						  ntinowwin += wintemp;
						  ntinowtie += tietemp;
						  ntinowlos += lostemp;
          }
		            
					// Calculations for vs$xx$prwin, vs$xx$prtie, vs$xx$prlos
					c0rank = StdDeck_RANK(i);
					c1rank = StdDeck_RANK(j);
					if (c0rank < c1rank)
					{
						temprank = c0rank;
						c0rank = c1rank;
						c1rank = temprank;
					}

          _n_win_against_hand[i][j] = wintemp;
          _n_tie_against_hand[i][j] = tietemp;
          _n_los_against_hand[i][j] = lostemp;
				}
			}
		}
	}

	// calc the stats
	_vsprwin = (double) _nwin / ((double)  _nwin + (double) _ntie + (double) _nlos);
	_vsprtie = (double) _ntie / ((double)  _nwin + (double) _ntie + (double) _nlos);
	_vsprlos = (double) _nlos / ((double)  _nwin + (double) _ntie + (double) _nlos);
   
   if(_nhandshi>0)
   {
	   _vsprwinhi = (double) nhiwin / ((double)  nhiwin + (double) nhitie + (double) nhilos);
	   _vsprtiehi = (double) nhitie / ((double)  nhiwin + (double) nhitie + (double) nhilos);
	   _vsprloshi = (double) nhilos / ((double)  nhiwin + (double) nhitie + (double) nhilos);
   }
   if(_nhandsti>0)
   {
	   _vsprwinti = (double) ntiwin / ((double)  ntiwin + (double) ntitie + (double) ntilos);
	   _vsprtieti = (double) ntitie / ((double)  ntiwin + (double) ntitie + (double) ntilos);
	   _vsprlosti = (double) ntilos / ((double)  ntiwin + (double) ntitie + (double) ntilos);
   }
   if(_nhandslo>0)
   {
	   _vsprwinlo = (double) nlowin / ((double)  nlowin + (double) nlotie + (double) nlolos);
	   _vsprtielo = (double) nlotie / ((double)  nlowin + (double) nlotie + (double) nlolos);
	   _vsprloslo = (double) nlolos / ((double)  nlowin + (double) nlotie + (double) nlolos);
   }
	if (betround >= k_betround_flop) 
	{
      if(_nhandshinow>0)
      {
         _vsprwinhinow = (double) nhinowwin / ((double)  nhinowwin + (double) nhinowtie + (double) nhinowlos);
	      _vsprtiehinow = (double) nhinowtie / ((double)  nhinowwin + (double) nhinowtie + (double) nhinowlos);
	      _vsprloshinow = (double) nhinowlos / ((double)  nhinowwin + (double) nhinowtie + (double) nhinowlos);
      }
      if(_nhandstinow>0)
      {
	      _vsprwintinow = (double) ntinowwin / ((double)  ntinowwin + (double) ntinowtie + (double) ntinowlos);
	      _vsprtietinow = (double) ntinowtie / ((double)  ntinowwin + (double) ntinowtie + (double) ntinowlos);
	      _vsprlostinow = (double) ntinowlos / ((double)  ntinowwin + (double) ntinowtie + (double) ntinowlos);
      }
      if(_nhandslonow>0)
      {
	      _vsprwinlonow = (double) nlonowwin / ((double)  nlonowwin + (double) nlonowtie + (double) nlonowlos);
	      _vsprtielonow = (double) nlonowtie / ((double)  nlonowwin + (double) nlonowtie + (double) nlonowlos);
	      _vsprloslonow = (double) nlonowlos / ((double)  nlonowwin + (double) nlonowtie + (double) nlonowlos);
      }
   }
	return true;
}

bool CSymbolEngineVersus::EvaluateSymbol(const char *name, double *result, bool log /* = false */) {
  FAST_EXIT_ON_OPENPPL_SYMBOLS(name);
  if (memcmp(name, "vs$", 3) != 0) {
    // Not a versus symbol
    return false;
  }
  if (!CheckForLoadedVersusBin()) {
		*result = k_undefined_zero;
    // We couldn't really evaluate this symbol,
    // but we have to return true to stop further evaluation
    // and to avoid further error-messages.
    return true;
  }
  write_log(preferences.debug_versus(),
    "[CVersus] EvaluateSymbol %s\n", name);
  if (memcmp(name, "vs$nhands", 9) == 0) {
    // vs$nhands...symbols
    if (memcmp(name, "vs$nhands", 9)==0 && strlen(name)==9)	            *result = _nhands;
    else if (memcmp(name, "vs$nhandshi", 11)==0 && strlen(name)==11)  	*result = _nhandshi;
	  else if (memcmp(name, "vs$nhandsti", 11)==0 && strlen(name)==11)    *result = _nhandsti;
	  else if (memcmp(name, "vs$nhandslo", 11)==0 && strlen(name)==11)	  *result = _nhandslo;
    else if (memcmp(name, "vs$nhandshinow", 14)==0 && strlen(name)==14)	*result = _nhandshinow;
	  else if (memcmp(name, "vs$nhandstinow", 14)==0 && strlen(name)==14)	*result = _nhandstinow;
	  else if (memcmp(name, "vs$nhandslonow", 14)==0 && strlen(name)==14)	*result = _nhandslonow;
    else {
      // Not a valid symbol
      return false;
    }
    return true;
  } else if (memcmp(name, "vs$pr", 5) == 0) {
    // vs$pr...symbols
    if (memcmp(name, "vs$prwin", 8)==0 && strlen(name)==8)		          *result = _vsprwin;
	  else if (memcmp(name, "vs$prtie", 8)==0 && strlen(name)==8)		      *result = _vsprtie;
	  else if (memcmp(name, "vs$prlos", 8)==0 && strlen(name)==8)		      *result = _vsprlos;
	  else if (memcmp(name, "vs$prwinhi", 10)==0 && strlen(name)==10)	    *result = _vsprwinhi;
	  else if (memcmp(name, "vs$prtiehi", 10)==0 && strlen(name)==10)	    *result = _vsprtiehi;
	  else if (memcmp(name, "vs$prloshi", 10)==0 && strlen(name)==10)	    *result = _vsprloshi;
	  else if (memcmp(name, "vs$prwinti", 10)==0 && strlen(name)==10)	    *result = _vsprwinti;
	  else if (memcmp(name, "vs$prtieti", 10)==0 && strlen(name)==10)	    *result = _vsprtieti;
	  else if (memcmp(name, "vs$prlosti", 10)==0 && strlen(name)==10)	    *result = _vsprlosti;
	  else if (memcmp(name, "vs$prwinlo", 10)==0 && strlen(name)==10)	    *result = _vsprwinlo;
	  else if (memcmp(name, "vs$prtielo", 10)==0 && strlen(name)==10)	    *result = _vsprtielo;
	  else if (memcmp(name, "vs$prloslo", 10)==0 && strlen(name)==10)	    *result = _vsprloslo;
	  else if (memcmp(name, "vs$prwinhinow", 13)==0 && strlen(name)==13)	*result = _vsprwinhinow;
	  else if (memcmp(name, "vs$prtiehinow", 13)==0 && strlen(name)==13)	*result = _vsprtiehinow;
	  else if (memcmp(name, "vs$prloshinow", 13)==0 && strlen(name)==13)	*result = _vsprloshinow;
	  else if (memcmp(name, "vs$prwintinow", 13)==0 && strlen(name)==13)	*result = _vsprwintinow;
	  else if (memcmp(name, "vs$prtietinow", 13)==0 && strlen(name)==13)	*result = _vsprtietinow;
	  else if (memcmp(name, "vs$prlostinow", 13)==0 && strlen(name)==13)	*result = _vsprlostinow;
	  else if (memcmp(name, "vs$prwinlonow", 13)==0 && strlen(name)==13)	*result = _vsprwinlonow;
	  else if (memcmp(name, "vs$prtielonow", 13)==0 && strlen(name)==13)	*result = _vsprtielonow;
	  else if (memcmp(name, "vs$prloslonow", 13)==0 && strlen(name)==13)	*result = _vsprloslonow;
    else {
      // Not a valid symbol
      return false;
    }
    return true;
  } else if (memcmp(name, "vs$list", 7) == 0) {
    // vs$list...$prwin/prtie/prlos-symbols
    bool valid_symbol = EvaluateVersusHandListSymbol(name, result, log);
    return valid_symbol;
  } else if (isdigit(name[3])) {
    OH_MessageBox_Formula_Error(
      "Old style versus-list format, like vs$123$win.\n"
      "No longer valid, as we do no longer have 1000 lists,\n"
      "but arbitrary many named lists.\n"
      "\n"
      "Example: vs$listTop30$prwin",
      "Error");
    *result = k_undefined_zero;
    return false;
  }
  ErrorInvalidSymbol(name);
  *result = k_undefined_zero;
  return false; 
}

void CSymbolEngineVersus::ErrorInvalidSymbol(CString name) {
  CString message;
  message.Format("Not a valid versus-symbol: %s", name);
  OH_MessageBox_Formula_Error(message, "Error");
}

bool CSymbolEngineVersus::EvaluateVersusHandListSymbol(const char *name, double *result, bool log /* = false */) {
  write_log(preferences.debug_versus(),
    "[CVersus] EvaluateVersusHandListSymbol %s\n", name);
  CString symbol = name;
  assert(symbol.Left(7) == "vs$list");
  CString postfix = symbol.Right(6);
  // List name contains everything except "vs$" and postfix
  int list_name_lenght = symbol.GetLength() - 9;
  if (list_name_lenght <= 0) {
    ErrorInvalidSymbol(name);
    *result = k_undefined_zero;
  }
  CString list_name = symbol.Mid(3, list_name_lenght);
  COHScriptList *hand_list = (COHScriptList*)p_function_collection->LookUp(list_name);
  if (hand_list == NULL) {
    // List not found
    // Symbol valid anyway
    *result = k_undefined_zero;
    return true;
  }
  GetCounts(); // !! not here
  double n_win = 0; 
  double n_tie = 0;
  double n_los = 0;
  write_log(preferences.debug_versus(),
    "[CVersus] EvaluateVersusHandListSymbol enumeration...\n");
  if (p_table_state->User()->HasKnownCards()) {
    // Versus makes only sense if we have known cards
    for (int i=0; i<(k_number_of_cards_per_deck - 1); i++) {
      for (int j=i+1; j<k_number_of_cards_per_deck; j++) {
        // StdDeck-ranks 0..12
        int c0rank = StdDeck_RANK(i);
		    int c1rank = StdDeck_RANK(j);
        int c0suit = StdDeck_SUIT(i);
		    int c1suit = StdDeck_SUIT(j);
        bool is_suited = (c0suit == c1suit);
        // OH-ranks 2..14
        int c0_OHrank = c0rank + 2;
        int c1_OHrank = c1rank + 2;
        write_log(preferences.debug_versus(),
          "[CVersus] Hand %d %d %s\n", 
          c0_OHrank, c1_OHrank, (c0_OHrank ? "suited": "offsuited"));
        if (hand_list->IsOnList(c0_OHrank, c1_OHrank, is_suited)) {
          write_log(preferences.debug_versus(), "[CVersus] Hand on list\n");
          // Hand in list
          // If not possible _n_win_against_hand etc. will be zero 
          write_log(preferences.debug_versus(), "[CVersus] Wins against this hand: %i\n", _n_win_against_hand[i][j]);
          assert(_n_win_against_hand[i][j] >= 0);
          assert(_n_tie_against_hand[i][j] >= 0);
          assert(_n_los_against_hand[i][j] >= 0);
          n_win += _n_win_against_hand[i][j];
          n_tie += _n_tie_against_hand[i][j];
          n_los += _n_los_against_hand[i][j];
        }
      }
    }
    write_log(preferences.debug_versus(),
      "[CVersus] Total hands on list: %d\n", hand_list->NHandsOnList());
  } 
  double n_total = n_win + n_tie + n_los;
  if (!p_table_state->User()->HasKnownCards() || (n_total == 0)) {
    // Unknown user-cards or empty list
    // Win / los / tie are zero.
    // Set total to 1 to avoid division by zero
    n_total = 1;
    write_log(preferences.debug_versus(),
      "[CVersus]Auto-adapting to 0/0/0, because user has no cards or empty list\n");
  }
  assert(n_win >= 0);
  assert(n_tie >= 0);
  assert(n_los >= 0);
  assert(n_total > 0);
  assert(hand_list->IsEmpty() || (n_total > 0));
  if (postfix == "$prwin") {
    *result = n_win / n_total;
    return true;
  } else if (postfix == "$prtie") {
    *result = n_tie / n_total;
    return true;
  } else if (postfix == "$prlos") {
    *result = n_los / n_total;
    return true;
  } else {
    ErrorInvalidSymbol(name);
    return false;
  }
}

CString CSymbolEngineVersus::SymbolsProvided() {
  return "vs$nhands vs$nhandshi vs$nhandsti vs$nhandslo "
    "vs$prwin vs$prtie vs$prlos vs$prwinhi vs$prtiehi vs$prloshi "
    "vs$prwinti vs$prtieti vs$prlosti vs$prwinlo vs$prtielo vs$prloslo "
    "vs$nhandshinow vs$nhandstinow vs$nhandslonow vs$prwinhinow "
    "vs$prtiehinow vs$prloshinow vs$prwintinow vs$prtietinow "
    "vs$prlostinow vs$prwinlonow vs$prtielonow vs$prloslonow "
    // vs$list-prefix
    "vs$list ";
}
