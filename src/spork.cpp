// Copyright (c) 2019 The BeeGroup developers are EternityGroup
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "spysend.h"
#include "main.h"
#include "spork.h"

#include <boost/lexical_cast.hpp>

class CSporkMessage;
class CSporkManager;

CSporkManager sporkManager;

std::map<uint256, CSporkMessage> mapSporks;

CEvolutionManager evolutionManager;

CCriticalSection cs_mapEvolution;
CCriticalSection cs_mapActive;


void CSporkManager::ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if(fLiteMode) return; // disable all Beenode specific functionality

    if (strCommand == NetMsgType::SPORK) {

        CDataStream vMsg(vRecv);
        CSporkMessage spork;
        vRecv >> spork;

        uint256 hash = spork.GetHash();

        std::string strLogMsg;
        {
            LOCK(cs_main);
            pfrom->setAskFor.erase(hash);
            if(!chainActive.Tip()) return;
            strLogMsg = strprintf("SPORK -- hash: %s id: %d value: %10d bestHeight: %d peer=%d", hash.ToString(), spork.nSporkID, spork.nValue, chainActive.Height(), pfrom->id);
        }
			
        if(isActiveSporkInMap(spork.nSporkID)) {
            if( getActiveSporkTime(spork.nSporkID) >= spork.nTimeSigned) {
                LogPrint("spork", "%s seen\n", strLogMsg);
                return;
            } else {
                LogPrintf("%s updated\n", strLogMsg);
            }
        } else {
            LogPrintf("%s new\n", strLogMsg);
        }

        if(!spork.CheckSignature()) {
            LogPrintf("CSporkManager::ProcessSpork -- invalid signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        mapSporks[hash] = spork;
		setActiveSpork( spork );
		if( spork.nSporkID == SPORK_18_EVOLUTION_PAYMENTS ){	
			evolutionManager.setNewEvolutions( spork.sWEvolution );
		}	
        spork.Relay();

        //does a task if needed
        ExecuteSpork(spork.nSporkID, spork.nValue);

    } else if (strCommand == NetMsgType::GETSPORKS) {
		LOCK( cs_mapActive );	
		std::map<int, CSporkMessage>::iterator it = mapSporksActive.begin();
		while(it != mapSporksActive.end()) {
			pfrom->PushMessage(NetMsgType::SPORK, it->second);
			it++;
		}
    }
}

void CSporkManager::ExecuteSpork(int nSporkID, int nValue)
{
    //correct fork via spork technology
    switch( nSporkID ) 
	{
	case SPORK_12_RECONSIDER_BLOCKS:
		if( nValue > 0) {
			// allow to reprocess 24h of blocks max, which should be enough to resolve any issues
			int64_t nMaxBlocks = 576;
			// this potentially can be a heavy operation, so only allow this to be executed once per 10 minutes
			int64_t nTimeout = 10 * 60;
			static int64_t nTimeExecuted = 0; // i.e. it was never executed before
			if(GetTime() - nTimeExecuted < nTimeout) {
				LogPrint("spork", "CSporkManager::ExecuteSpork -- ERROR: Trying to reconsider blocks, too soon - %d/%d\n", GetTime() - nTimeExecuted, nTimeout);
				return;
			}
			if(nValue > nMaxBlocks) {
				LogPrintf("CSporkManager::ExecuteSpork -- ERROR: Trying to reconsider too many blocks %d/%d\n", nValue, nMaxBlocks);
				return;
			}
			LogPrintf("CSporkManager::ExecuteSpork -- Reconsider Last %d Blocks\n", nValue);
			ReprocessBlocks(nValue);
			nTimeExecuted = GetTime();
		}
		break;
	};
}

bool CSporkManager::UpdateSpork(int nSporkID, int64_t nValue, std::string sEvol)
{
    CSporkMessage spork = CSporkMessage(nSporkID, nValue, sEvol, GetTime());
	
    if(spork.Sign(strMasterPrivKey)) {
        spork.Relay();
        
		mapSporks[spork.GetHash()] = spork;
		setActiveSpork( spork );		
		if(nSporkID == SPORK_18_EVOLUTION_PAYMENTS){
			evolutionManager.setNewEvolutions( sEvol );
		}		
		
        return true;
    }

    return false;
}

void CSporkManager::setActiveSpork( CSporkMessage &spork )
{
	LOCK( cs_mapActive );
	mapSporksActive[spork.nSporkID] = spork;
}

int64_t CSporkManager::getActiveSporkValue( int nSporkID )
{
	int64_t r = -1;
	
	LOCK( cs_mapActive );
	
    if( mapSporksActive.count(nSporkID) ) r = mapSporksActive[nSporkID].nValue;
	
	return r;
}

bool CSporkManager::isActiveSporkInMap(int nSporkID)
{
	LOCK( cs_mapActive );	
	return mapSporksActive.count(nSporkID);
}

int64_t CSporkManager::getActiveSporkTime(int nSporkID)
{
	LOCK( cs_mapActive );	
	return mapSporksActive[nSporkID].nTimeSigned;
}

// grab the spork, otherwise say it's off
bool CSporkManager::IsSporkActive(int nSporkID)
{
	int64_t r = getActiveSporkValue( nSporkID );	
	if( r < 0 ){
        switch (nSporkID) {
            case SPORK_2_INSTANTSEND_ENABLED:               r = SPORK_2_INSTANTSEND_ENABLED_DEFAULT; break;
            case SPORK_3_INSTANTSEND_BLOCK_FILTERING:       r = SPORK_3_INSTANTSEND_BLOCK_FILTERING_DEFAULT; break;
            case SPORK_5_INSTANTSEND_MAX_VALUE:             r = SPORK_5_INSTANTSEND_MAX_VALUE_DEFAULT; break;
            case SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT:    r = SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT; break;
            case SPORK_10_MASTERNODE_PAY_UPDATED_NODES:     r = SPORK_10_MASTERNODE_PAY_UPDATED_NODES_DEFAULT; break;
            case SPORK_11_MASTERNODE_ENABLED:     			r = SPORK_11_MASTERNODE_ENABLED_DEFAULT; break;
			case SPORK_12_RECONSIDER_BLOCKS:                r = SPORK_12_RECONSIDER_BLOCKS_DEFAULT; break;
            case SPORK_13_OLD_SUPERBLOCK_FLAG:              r = SPORK_13_OLD_SUPERBLOCK_FLAG_DEFAULT; break;
            case SPORK_14_REQUIRE_SENTINEL_FLAG:            r = SPORK_14_REQUIRE_SENTINEL_FLAG_DEFAULT; break;
			case SPORK_18_EVOLUTION_PAYMENTS:				r = SPORK_18_EVOLUTION_PAYMENTS_DEFAULT; break;
			case SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT:   r = SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT_DEFAULT; break;
			
            default:
                LogPrint("spork", "CSporkManager::IsSporkActive -- Unknown Spork ID %d\n", nSporkID);
                r = 4070908800ULL; // 2099-1-1 i.e. off by default
                break;
        }
    }
    return r < GetTime();
}

// grab the spork, otherwise say it's off
bool CSporkManager::IsSporkWorkActive(int nSporkID)
{
	int64_t r = getActiveSporkValue( nSporkID );	
	if( r < 0 ){
        switch (nSporkID) {
            case SPORK_2_INSTANTSEND_ENABLED:               r = SPORK_2_INSTANTSEND_ENABLED_DEFAULT; break;
            case SPORK_3_INSTANTSEND_BLOCK_FILTERING:       r = SPORK_3_INSTANTSEND_BLOCK_FILTERING_DEFAULT; break;
            case SPORK_5_INSTANTSEND_MAX_VALUE:             r = SPORK_5_INSTANTSEND_MAX_VALUE_DEFAULT; break;
            case SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT:    r = SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT; break;
            case SPORK_10_MASTERNODE_PAY_UPDATED_NODES:     r = SPORK_10_MASTERNODE_PAY_UPDATED_NODES_DEFAULT; break;
            case SPORK_11_MASTERNODE_ENABLED:     			r = SPORK_11_MASTERNODE_ENABLED_DEFAULT; break;
			case SPORK_12_RECONSIDER_BLOCKS:                r = SPORK_12_RECONSIDER_BLOCKS_DEFAULT; break;
            case SPORK_13_OLD_SUPERBLOCK_FLAG:              r = SPORK_13_OLD_SUPERBLOCK_FLAG_DEFAULT; break;
            case SPORK_14_REQUIRE_SENTINEL_FLAG:            r = SPORK_14_REQUIRE_SENTINEL_FLAG_DEFAULT; break;
			case SPORK_18_EVOLUTION_PAYMENTS:				r = SPORK_18_EVOLUTION_PAYMENTS_DEFAULT; break;
			case SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT:   r = SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT_DEFAULT; break;
			
            default:
                LogPrint("spork", "CSporkManager::IsSporkActive -- Unknown Spork ID %d\n", nSporkID);
                r = 0; // 2099-1-1 i.e. off by default
                break;
        }
    }
    return r > 0;
}

// grab the value of the spork on the network, or the default
int64_t CSporkManager::GetSporkValue(int nSporkID)
{
	int64_t r = getActiveSporkValue( nSporkID );	
	if(  r > 0 )
		return r;
	
    switch (nSporkID) {
        case SPORK_2_INSTANTSEND_ENABLED:               return SPORK_2_INSTANTSEND_ENABLED_DEFAULT;
        case SPORK_3_INSTANTSEND_BLOCK_FILTERING:       return SPORK_3_INSTANTSEND_BLOCK_FILTERING_DEFAULT;
        case SPORK_5_INSTANTSEND_MAX_VALUE:             return SPORK_5_INSTANTSEND_MAX_VALUE_DEFAULT;
        case SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT:    return SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT;
        case SPORK_10_MASTERNODE_PAY_UPDATED_NODES:     return SPORK_10_MASTERNODE_PAY_UPDATED_NODES_DEFAULT;
        case SPORK_11_MASTERNODE_ENABLED:     			return SPORK_11_MASTERNODE_ENABLED_DEFAULT;
		case SPORK_12_RECONSIDER_BLOCKS:                return SPORK_12_RECONSIDER_BLOCKS_DEFAULT;
        case SPORK_13_OLD_SUPERBLOCK_FLAG:              return SPORK_13_OLD_SUPERBLOCK_FLAG_DEFAULT;
        case SPORK_14_REQUIRE_SENTINEL_FLAG:            return SPORK_14_REQUIRE_SENTINEL_FLAG_DEFAULT;
		case SPORK_18_EVOLUTION_PAYMENTS:				return SPORK_18_EVOLUTION_PAYMENTS_DEFAULT;
		case SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT:   return SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT_DEFAULT;		
		
        default:
            LogPrint("spork", "CSporkManager::GetSporkValue -- Unknown Spork ID %d\n", nSporkID);
            return -1;
    }
}

int CSporkManager::GetSporkIDByName(std::string strName)
{
    if (strName == "SPORK_2_INSTANTSEND_ENABLED")               return SPORK_2_INSTANTSEND_ENABLED;
    if (strName == "SPORK_3_INSTANTSEND_BLOCK_FILTERING")       return SPORK_3_INSTANTSEND_BLOCK_FILTERING;
    if (strName == "SPORK_5_INSTANTSEND_MAX_VALUE")             return SPORK_5_INSTANTSEND_MAX_VALUE;
    if (strName == "SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT")    return SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT;
    if (strName == "SPORK_10_MASTERNODE_PAY_UPDATED_NODES")     return SPORK_10_MASTERNODE_PAY_UPDATED_NODES;
    if (strName == "SPORK_11_MASTERNODE_ENABLED")     			return SPORK_11_MASTERNODE_ENABLED;
	if (strName == "SPORK_12_RECONSIDER_BLOCKS")                return SPORK_12_RECONSIDER_BLOCKS;
    if (strName == "SPORK_13_OLD_SUPERBLOCK_FLAG")              return SPORK_13_OLD_SUPERBLOCK_FLAG;
    if (strName == "SPORK_14_REQUIRE_SENTINEL_FLAG")            return SPORK_14_REQUIRE_SENTINEL_FLAG;
	if (strName == "SPORK_18_EVOLUTION_PAYMENTS")				return SPORK_18_EVOLUTION_PAYMENTS;
    if (strName == "SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT")   return SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT;

    LogPrint("spork", "CSporkManager::GetSporkIDByName -- Unknown Spork name '%s'\n", strName);
    return -1;
}

std::string CSporkManager::GetSporkNameByID(int nSporkID)
{
    switch (nSporkID) {
        case SPORK_2_INSTANTSEND_ENABLED:               return "SPORK_2_INSTANTSEND_ENABLED";
        case SPORK_3_INSTANTSEND_BLOCK_FILTERING:       return "SPORK_3_INSTANTSEND_BLOCK_FILTERING";
        case SPORK_5_INSTANTSEND_MAX_VALUE:             return "SPORK_5_INSTANTSEND_MAX_VALUE";
        case SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT:    return "SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT";
        case SPORK_10_MASTERNODE_PAY_UPDATED_NODES:     return "SPORK_10_MASTERNODE_PAY_UPDATED_NODES";
        case SPORK_11_MASTERNODE_ENABLED:     			return "SPORK_11_MASTERNODE_ENABLED";
        case SPORK_12_RECONSIDER_BLOCKS:                return "SPORK_12_RECONSIDER_BLOCKS";
        case SPORK_13_OLD_SUPERBLOCK_FLAG:              return "SPORK_13_OLD_SUPERBLOCK_FLAG";
        case SPORK_14_REQUIRE_SENTINEL_FLAG:            return "SPORK_14_REQUIRE_SENTINEL_FLAG";
		case SPORK_18_EVOLUTION_PAYMENTS:				return "SPORK_18_EVOLUTION_PAYMENTS";
		case SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT:   return "SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT";        
		default:
            LogPrint("spork", "CSporkManager::GetSporkNameByID -- Unknown Spork ID %d\n", nSporkID);
            return "Unknown";
    }
}

bool CSporkManager::SetPrivKey(std::string strPrivKey)
{
    CSporkMessage spork;

    spork.Sign(strPrivKey);

    if(spork.CheckSignature()){
        // Test signing successful, proceed
        LogPrintf("CSporkManager::SetPrivKey -- Successfully initialized as spork signer\n");
        strMasterPrivKey = strPrivKey;
        return true;
    } else {
        return false;
    }
}

bool CSporkMessage::Sign(std::string strSignKey)
{
    CKey key;
    CPubKey pubkey;
    std::string strError = "";
    std::string strMessage = boost::lexical_cast<std::string>(nSporkID) + boost::lexical_cast<std::string>(nValue) + boost::lexical_cast<std::string>(nTimeSigned);

    if(!spySendSigner.GetKeysFromSecret(strSignKey, key, pubkey)) {
        LogPrintf("CSporkMessage::Sign -- GetKeysFromSecret() failed, invalid spork key %s\n", strSignKey);
        return false;
    }

    if(!spySendSigner.SignMessage(strMessage, vchSig, key)) {
        LogPrintf("CSporkMessage::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubkey, vchSig, strMessage, strError)) {
        LogPrintf("CSporkMessage::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CSporkMessage::CheckSignature()
{
    //note: need to investigate why this is failing
    std::string strError = "";
    std::string strMessage = boost::lexical_cast<std::string>(nSporkID) + boost::lexical_cast<std::string>(nValue) + boost::lexical_cast<std::string>(nTimeSigned);
    CPubKey pubkey(ParseHex(Params().SporkPubKey()));

    if(!spySendSigner.VerifyMessage(pubkey, vchSig, strMessage, strError)) {
        LogPrintf("CSporkMessage::CheckSignature -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

void CSporkMessage::Relay()
{
    CInv inv(MSG_SPORK, GetHash());
    RelayInv(inv);
}

void CEvolutionManager::setNewEvolutions( const std::string &sEvol )
{
	LOCK( cs_mapEvolution );
	
	unsigned long bComplete = 0;
	unsigned long uCountEvolution = 0;
	unsigned long uStart = 0;
	
	mapEvolution.clear();

	for( unsigned int i = 0; i < sEvol.size() ; i++ )
	{
		if( (sEvol.c_str()[i] == '[') && (bComplete == 0) ){
			bComplete = 1;
			uStart = i + 1;
		}

		if( (sEvol.c_str()[i] == ',') && (bComplete == 1)  ){
			mapEvolution.insert(  make_pair( uCountEvolution, std::string(sEvol.c_str() + uStart, i-uStart))  );
			uCountEvolution++;
			uStart = i + 1;
		}

		if(  (sEvol.c_str()[i] == ']') && (bComplete == 1)  ){
			bComplete = 2;
			mapEvolution.insert(  make_pair( uCountEvolution, std::string(sEvol.c_str() + uStart, i - uStart))  );
			uCountEvolution++;
		}
	}
}

std::string CEvolutionManager::getEvolution( int nBlockHeight )
{	
	LOCK( cs_mapEvolution );
	
	return mapEvolution[ nBlockHeight%mapEvolution.size() ];
}	

bool CEvolutionManager::IsTransactionValid( const CTransaction& txNew, int nBlockHeight, CAmount blockCurEvolution  )
{	
	LOCK( cs_mapEvolution );
	
	if( mapEvolution.size() < 1 ){ 
		return true;
	}
	
	CTxDestination address1;

	for( unsigned int i = 0; i < txNew.vout.size(); i++ )
	{	
		ExtractDestination(txNew.vout[i].scriptPubKey, address1);
		CBitcoinAddress address2(address1);

		if(  ( mapEvolution[nBlockHeight%mapEvolution.size()] == address2.ToString() ) && (blockCurEvolution == txNew.vout[i].nValue)  ){
			return true;
		}
	}	

	return false;
}
	
bool CEvolutionManager::checkEvolutionString( const std::string &sEvol )
{	
	unsigned long bComplete = 0;

	for (unsigned int i = 0; i < sEvol.size(); i++)
	{
		if (sEvol.c_str()[i] == '[')
		{
			bComplete = 1;
		}

		if ((sEvol.c_str()[i] == ']') && (bComplete == 1))
		{
			bComplete = 2;
		}
	}

	if( bComplete == 2 ){
		return true;
	}

	return false;
}
