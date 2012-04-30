// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2011-2012 Litecoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "headers.h"
#include "db.h"
#include "net.h"
#include "init.h"
#undef printf
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#ifdef USE_SSL
#include <boost/asio/ssl.hpp> 
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> SSLStream;
#endif
#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include "json/json_spirit_utils.h"
#define printf OutputDebugStringF
// MinGW 3.4.5 gets "fatal error: had to relocate PCH" if the json headers are
// precompiled in headers.h.  The problem might be when the pch file goes over
// a certain size around 145MB.  If we need access to json_spirit outside this
// file, we could use the compiled json_spirit option.

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace json_spirit;

void ThreadRPCServer2(void* parg);
typedef Value(*rpcfn_type)(const Array& params, bool fHelp);
extern map<string, rpcfn_type> mapCallTable;

static std::string strRPCUserColonPass;

static int64 nWalletUnlockTime;
static CCriticalSection cs_nWalletUnlockTime;

extern Value dumpprivkey(const Array& params, bool fHelp);
extern Value importprivkey(const Array& params, bool fHelp);

Object JSONRPCError(int code, const string& message)
{
    Object error;
    error.push_back(Pair("code", code));
    error.push_back(Pair("message", message));
    return error;
}

double GetDifficulty(const CBlockIndex* blockindex = NULL)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (pindexBest == NULL)
            return 1.0;
        else
            blockindex = pindexBest;
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}


int64 AmountFromValue(const Value& value)
{
    double dAmount = value.get_real();
    if (dAmount <= 0.0 || dAmount > 84000000.0)
        throw JSONRPCError(-3, "Invalid amount");
    int64 nAmount = roundint64(dAmount * COIN);
    if (!MoneyRange(nAmount))
        throw JSONRPCError(-3, "Invalid amount");
    return nAmount;
}

Value ValueFromAmount(int64 amount)
{
    return (double)amount / (double)COIN;
}

std::string
HexBits(unsigned int nBits)
{
    union {
        int32_t nBits;
        char cBits[4];
    } uBits;
    uBits.nBits = htonl((int32_t)nBits);
    return HexStr(BEGIN(uBits.cBits), END(uBits.cBits));
}

void WalletTxToJSON(const CWalletTx& wtx, Object& entry)
{
    int confirms = wtx.GetDepthInMainChain();
    entry.push_back(Pair("confirmations", confirms));
    if (confirms)
    {
        entry.push_back(Pair("blockhash", wtx.hashBlock.GetHex()));
        entry.push_back(Pair("blockindex", wtx.nIndex));
    }
    entry.push_back(Pair("txid", wtx.GetHash().GetHex()));
    entry.push_back(Pair("time", (boost::int64_t)wtx.GetTxTime()));
    BOOST_FOREACH(const PAIRTYPE(string,string)& item, wtx.mapValue)
        entry.push_back(Pair(item.first, item.second));
}

string AccountFromValue(const Value& value)
{
    string strAccount = value.get_str();
    if (strAccount == "*")
        throw JSONRPCError(-11, "Invalid account name");
    return strAccount;
}

Object blockToJSON(const CBlock& block, const CBlockIndex* blockindex)
{
    Object result;
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(Pair("time", (boost::int64_t)block.GetBlockTime()));
    result.push_back(Pair("nonce", (boost::uint64_t)block.nNonce));
    result.push_back(Pair("bits", HexBits(block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    Array txhashes;
    BOOST_FOREACH (const CTransaction&tx, block.vtx)
        txhashes.push_back(tx.GetHash().GetHex());
    result.push_back(Pair("tx", txhashes));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    if (blockindex->pnext)
        result.push_back(Pair("nextblockhash", blockindex->pnext->GetBlockHash().GetHex()));
    return result;
}



///
/// Note: This interface may still be subject to change.
///


Value help(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "help [command]\n"
            "List commands, or get help for a command.");

    string strCommand;
    if (params.size() > 0)
        strCommand = params[0].get_str();

    string strRet;
    set<rpcfn_type> setDone;
    for (map<string, rpcfn_type>::iterator mi = mapCallTable.begin(); mi != mapCallTable.end(); ++mi)
    {
        string strMethod = (*mi).first;
        // We already filter duplicates, but these deprecated screw up the sort order
        if (strMethod == "getamountreceived" ||
            strMethod == "getallreceived" ||
            strMethod == "getblocknumber" || // deprecated
            (strMethod.find("label") != string::npos))
            continue;
        if (strCommand != "" && strMethod != strCommand)
            continue;
        try
        {
            Array params;
            rpcfn_type pfn = (*mi).second;
            if (setDone.insert(pfn).second)
                (*pfn)(params, true);
        }
        catch (std::exception& e)
        {
            // Help text is returned in an exception
            string strHelp = string(e.what());
            if (strCommand == "")
                if (strHelp.find('\n') != -1)
                    strHelp = strHelp.substr(0, strHelp.find('\n'));
            strRet += strHelp + "\n";
        }
    }
    if (strRet == "")
        strRet = strprintf("help: unknown command: %s\n", strCommand.c_str());
    strRet = strRet.substr(0,strRet.size()-1);
    return strRet;
}


Value stop(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "stop\n"
            "Stop litecoin server.");
#ifndef QT_GUI
    // Shutdown will take long enough that the response should get back
    CreateThread(Shutdown, NULL);
    return "litecoin server stopping";
#else
    throw runtime_error("NYI: cannot shut down GUI with RPC command");
#endif
}


Value getblockcount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "Returns the number of blocks in the longest block chain.");

    return nBestHeight;
}


// deprecated
Value getblocknumber(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblocknumber\n"
            "Deprecated.  Use getblockcount.");

    return nBestHeight;
}


Value getconnectioncount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getconnectioncount\n"
            "Returns the number of connections to other nodes.");

    return (int)vNodes.size();
}


// Litecoin: Return average network hashes per second based on last number of blocks.
int GetNetworkHashPS(int lookup) {
    if (pindexBest == NULL)
        return 0;

    // If lookup is -1, then use blocks since last difficulty change.
    if (lookup <= 0)
        lookup = pindexBest->nHeight % 2016 + 1;

    // If lookup is larger than chain, then set it to chain length.
    if (lookup > pindexBest->nHeight)
        lookup = pindexBest->nHeight;

    CBlockIndex* pindexPrev = pindexBest;
    for (int i = 0; i < lookup; i++)
        pindexPrev = pindexPrev->pprev;

    double timeDiff = pindexBest->GetBlockTime() - pindexPrev->GetBlockTime();
    double timePerBlock = timeDiff / lookup;

    return (int)(((double)GetDifficulty() * pow(2.0, 32)) / timePerBlock);
}

Value getdifficulty(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "Returns the proof-of-work difficulty as a multiple of the minimum difficulty.");

    return GetDifficulty();
}


Value getnetworkhashps(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getnetworkhashps [blocks]\n"
            "Returns the estimated network hashes per second based on the last 120 blocks.\n"
            "Pass in [blocks] to override # of blocks, -1 specifies since last difficulty change.");

    return GetNetworkHashPS(params.size() > 0 ? params[0].get_int() : 120);
}


Value getgenerate(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getgenerate\n"
            "Returns true or false.");

    return GetBoolArg("-gen");
}


Value setgenerate(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setgenerate <generate> [genproclimit]\n"
            "<generate> is true or false to turn generation on or off.\n"
            "Generation is limited to [genproclimit] processors, -1 is unlimited.");

    bool fGenerate = true;
    if (params.size() > 0)
        fGenerate = params[0].get_bool();

    if (params.size() > 1)
    {
        int nGenProcLimit = params[1].get_int();
        mapArgs["-genproclimit"] = itostr(nGenProcLimit);
        if (nGenProcLimit == 0)
            fGenerate = false;
    }
    mapArgs["-gen"] = (fGenerate ? "1" : "0");

    GenerateBitcoins(fGenerate, pwalletMain);
    return Value::null;
}


Value gethashespersec(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "gethashespersec\n"
            "Returns a recent hashes per second performance measurement while generating.");

    if (GetTimeMillis() - nHPSTimerStart > 8000)
        return (boost::int64_t)0;
    return (boost::int64_t)dHashesPerSec;
}


Value getinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getinfo\n"
            "Returns an object containing various state info.");

    Object obj;
    obj.push_back(Pair("version",       (int)CLIENT_VERSION));
    obj.push_back(Pair("protocolversion",(int)PROTOCOL_VERSION));
    obj.push_back(Pair("balance",       ValueFromAmount(pwalletMain->GetBalance())));
    obj.push_back(Pair("blocks",        (int)nBestHeight));
    obj.push_back(Pair("connections",   (int)vNodes.size()));
    obj.push_back(Pair("proxy",         (fUseProxy ? addrProxy.ToStringIPPort() : string())));
    obj.push_back(Pair("difficulty",    (double)GetDifficulty()));
    obj.push_back(Pair("hashespersec",  gethashespersec(params, false)));
    obj.push_back(Pair("networkhashps", getnetworkhashps(params, false)));
    obj.push_back(Pair("testnet",       fTestNet));
    obj.push_back(Pair("keypoololdest", (boost::int64_t)pwalletMain->GetOldestKeyPoolTime()));
    obj.push_back(Pair("keypoolsize",   pwalletMain->GetKeyPoolSize()));
    obj.push_back(Pair("paytxfee",      ValueFromAmount(nTransactionFee)));
    obj.push_back(Pair("mininput",      ValueFromAmount(nMinimumInputValue)));
    if (pwalletMain->IsCrypted())
        obj.push_back(Pair("unlocked_until", (boost::int64_t)nWalletUnlockTime / 1000));
    obj.push_back(Pair("errors",        GetWarnings("statusbar")));
    return obj;
}


Value getmininginfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getmininginfo\n"
            "Returns an object containing mining-related information.");

    Object obj;
    obj.push_back(Pair("blocks",        (int)nBestHeight));
    obj.push_back(Pair("currentblocksize",(uint64_t)nLastBlockSize));
    obj.push_back(Pair("currentblocktx",(uint64_t)nLastBlockTx));
    obj.push_back(Pair("difficulty",    (double)GetDifficulty()));
    obj.push_back(Pair("errors",        GetWarnings("statusbar")));
    obj.push_back(Pair("generate",      GetBoolArg("-gen")));
    obj.push_back(Pair("genproclimit",  (int)GetArg("-genproclimit", -1)));
    obj.push_back(Pair("hashespersec",  gethashespersec(params, false)));
    obj.push_back(Pair("pooledtx",      (uint64_t)nPooledTx));
    obj.push_back(Pair("testnet",       fTestNet));
    return obj;
}


Value getnewaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getnewaddress [account]\n"
            "Returns a new litecoin address for receiving payments.  "
            "If [account] is specified (recommended), it is added to the address book "
            "so payments received with the address will be credited to [account].");

    // Parse the account first so we don't generate a key if there's an error
    string strAccount;
    if (params.size() > 0)
        strAccount = AccountFromValue(params[0]);

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    // Generate a new key that is added to wallet
    std::vector<unsigned char> newKey;
    if (!pwalletMain->GetKeyFromPool(newKey, false))
        throw JSONRPCError(-12, "Error: Keypool ran out, please call keypoolrefill first");
    CBitcoinAddress address(newKey);

    pwalletMain->SetAddressBookName(address, strAccount);

    return address.ToString();
}


CBitcoinAddress GetAccountAddress(string strAccount, bool bForceNew=false)
{
    CWalletDB walletdb(pwalletMain->strWalletFile);

    CAccount account;
    walletdb.ReadAccount(strAccount, account);

    bool bKeyUsed = false;

    // Check if the current key has been used
    if (!account.vchPubKey.empty())
    {
        CScript scriptPubKey;
        scriptPubKey.SetBitcoinAddress(account.vchPubKey);
        for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin();
             it != pwalletMain->mapWallet.end() && !account.vchPubKey.empty();
             ++it)
        {
            const CWalletTx& wtx = (*it).second;
            BOOST_FOREACH(const CTxOut& txout, wtx.vout)
                if (txout.scriptPubKey == scriptPubKey)
                    bKeyUsed = true;
        }
    }

    // Generate a new key
    if (account.vchPubKey.empty() || bForceNew || bKeyUsed)
    {
        if (!pwalletMain->GetKeyFromPool(account.vchPubKey, false))
            throw JSONRPCError(-12, "Error: Keypool ran out, please call keypoolrefill first");

        pwalletMain->SetAddressBookName(CBitcoinAddress(account.vchPubKey), strAccount);
        walletdb.WriteAccount(strAccount, account);
    }

    return CBitcoinAddress(account.vchPubKey);
}

Value getaccountaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getaccountaddress <account>\n"
            "Returns the current litecoin address for receiving payments to this account.");

    // Parse the account first so we don't generate a key if there's an error
    string strAccount = AccountFromValue(params[0]);

    Value ret;

    ret = GetAccountAddress(strAccount).ToString();

    return ret;
}



Value setaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setaccount <litecoinaddress> <account>\n"
            "Sets the account associated with the given address.");

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(-5, "Invalid litecoin address");


    string strAccount;
    if (params.size() > 1)
        strAccount = AccountFromValue(params[1]);

    // Detect when changing the account of an address that is the 'unused current key' of another account:
    if (pwalletMain->mapAddressBook.count(address))
    {
        string strOldAccount = pwalletMain->mapAddressBook[address];
        if (address == GetAccountAddress(strOldAccount))
            GetAccountAddress(strOldAccount, true);
    }

    pwalletMain->SetAddressBookName(address, strAccount);

    return Value::null;
}


Value getaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getaccount <litecoinaddress>\n"
            "Returns the account associated with the given address.");

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(-5, "Invalid litecoin address");

    string strAccount;
    map<CBitcoinAddress, string>::iterator mi = pwalletMain->mapAddressBook.find(address);
    if (mi != pwalletMain->mapAddressBook.end() && !(*mi).second.empty())
        strAccount = (*mi).second;
    return strAccount;
}


Value getaddressesbyaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getaddressesbyaccount <account>\n"
            "Returns the list of addresses for the given account.");

    string strAccount = AccountFromValue(params[0]);

    // Find all addresses that have the given account
    Array ret;
    BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, string)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        const string& strName = item.second;
        if (strName == strAccount)
            ret.push_back(address.ToString());
    }
    return ret;
}

Value settxfee(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1)
        throw runtime_error(
            "settxfee <amount>\n"
            "<amount> is a real and is rounded to the nearest 0.00000001");

    // Amount
    int64 nAmount = 0;
    if (params[0].get_real() != 0.0)
        nAmount = AmountFromValue(params[0]);        // rejects 0.0 amounts

    nTransactionFee = nAmount;
    return true;
}

Value setmininput(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1)
        throw runtime_error(
            "setmininput <amount>\n"
            "<amount> is a real and is rounded to the nearest 0.00000001");

    // Amount
    int64 nAmount = 0;
    if (params[0].get_real() != 0.0)
        nAmount = AmountFromValue(params[0]);        // rejects 0.0 amounts

    nMinimumInputValue = nAmount;
    return true;
}

Value sendtoaddress(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() < 2 || params.size() > 4))
        throw runtime_error(
            "sendtoaddress <litecoinaddress> <amount> [comment] [comment-to]\n"
            "<amount> is a real and is rounded to the nearest 0.00000001\n"
            "requires wallet passphrase to be set with walletpassphrase first");
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() < 2 || params.size() > 4))
        throw runtime_error(
            "sendtoaddress <litecoinaddress> <amount> [comment] [comment-to]\n"
            "<amount> is a real and is rounded to the nearest 0.00000001");

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(-5, "Invalid litecoin address");

    // Amount
    int64 nAmount = AmountFromValue(params[1]);

    // Wallet comments
    CWalletTx wtx;
    if (params.size() > 2 && params[2].type() != null_type && !params[2].get_str().empty())
        wtx.mapValue["comment"] = params[2].get_str();
    if (params.size() > 3 && params[3].type() != null_type && !params[3].get_str().empty())
        wtx.mapValue["to"]      = params[3].get_str();

    if (pwalletMain->IsLocked())
        throw JSONRPCError(-13, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    string strError = pwalletMain->SendMoneyToBitcoinAddress(address, nAmount, wtx);
    if (strError != "")
        throw JSONRPCError(-4, strError);

    return wtx.GetHash().GetHex();
}

Value signmessage(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "signmessage <litecoinaddress> <message>\n"
            "Sign a message with the private key of an address");

    if (pwalletMain->IsLocked())
        throw JSONRPCError(-13, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    string strAddress = params[0].get_str();
    string strMessage = params[1].get_str();

    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(-3, "Invalid address");

    CKey key;
    if (!pwalletMain->GetKey(addr, key))
        throw JSONRPCError(-4, "Private key not available");

    CDataStream ss(SER_GETHASH);
    ss << strMessageMagic;
    ss << strMessage;

    vector<unsigned char> vchSig;
    if (!key.SignCompact(Hash(ss.begin(), ss.end()), vchSig))
        throw JSONRPCError(-5, "Sign failed");

    return EncodeBase64(&vchSig[0], vchSig.size());
}

Value verifymessage(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "verifymessage <litecoinaddress> <signature> <message>\n"
            "Verify a signed message");

    string strAddress  = params[0].get_str();
    string strSign     = params[1].get_str();
    string strMessage  = params[2].get_str();

    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(-3, "Invalid address");

    bool fInvalid = false;
    vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(-5, "Malformed base64 encoding");

    CDataStream ss(SER_GETHASH);
    ss << strMessageMagic;
    ss << strMessage;

    CKey key;
    if (!key.SetCompactSignature(Hash(ss.begin(), ss.end()), vchSig))
        return false;

    return (CBitcoinAddress(key.GetPubKey()) == addr);
}


Value getreceivedbyaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getreceivedbyaddress <litecoinaddress> [minconf=1]\n"
            "Returns the total amount received by <litecoinaddress> in transactions with at least [minconf] confirmations.");

    // Litecoin address
    CBitcoinAddress address = CBitcoinAddress(params[0].get_str());
    CScript scriptPubKey;
    if (!address.IsValid())
        throw JSONRPCError(-5, "Invalid litecoin address");
    scriptPubKey.SetBitcoinAddress(address);
    if (!IsMine(*pwalletMain,scriptPubKey))
        return (double)0.0;

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    // Tally
    int64 nAmount = 0;
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (wtx.IsCoinBase() || !wtx.IsFinal())
            continue;

        BOOST_FOREACH(const CTxOut& txout, wtx.vout)
            if (txout.scriptPubKey == scriptPubKey)
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
    }

    return  ValueFromAmount(nAmount);
}


void GetAccountAddresses(string strAccount, set<CBitcoinAddress>& setAddress)
{
    BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, string)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        const string& strName = item.second;
        if (strName == strAccount)
            setAddress.insert(address);
    }
}


Value getreceivedbyaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getreceivedbyaccount <account> [minconf=1]\n"
            "Returns the total amount received by addresses with <account> in transactions with at least [minconf] confirmations.");

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    // Get the set of pub keys assigned to account
    string strAccount = AccountFromValue(params[0]);
    set<CBitcoinAddress> setAddress;
    GetAccountAddresses(strAccount, setAddress);

    // Tally
    int64 nAmount = 0;
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (wtx.IsCoinBase() || !wtx.IsFinal())
            continue;

        BOOST_FOREACH(const CTxOut& txout, wtx.vout)
        {
            CBitcoinAddress address;
            if (ExtractAddress(txout.scriptPubKey, address) && pwalletMain->HaveKey(address) && setAddress.count(address))
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
        }
    }

    return (double)nAmount / (double)COIN;
}


int64 GetAccountBalance(CWalletDB& walletdb, const string& strAccount, int nMinDepth)
{
    int64 nBalance = 0;

    // Tally wallet transactions
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (!wtx.IsFinal())
            continue;

        int64 nGenerated, nReceived, nSent, nFee;
        wtx.GetAccountAmounts(strAccount, nGenerated, nReceived, nSent, nFee);

        if (nReceived != 0 && wtx.GetDepthInMainChain() >= nMinDepth)
            nBalance += nReceived;
        nBalance += nGenerated - nSent - nFee;
    }

    // Tally internal accounting entries
    nBalance += walletdb.GetAccountCreditDebit(strAccount);

    return nBalance;
}

int64 GetAccountBalance(const string& strAccount, int nMinDepth)
{
    CWalletDB walletdb(pwalletMain->strWalletFile);
    return GetAccountBalance(walletdb, strAccount, nMinDepth);
}


Value getbalance(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "getbalance [account] [minconf=1]\n"
            "If [account] is not specified, returns the server's total available balance.\n"
            "If [account] is specified, returns the balance in the account.");

    if (params.size() == 0)
        return  ValueFromAmount(pwalletMain->GetBalance());

    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    if (params[0].get_str() == "*") {
        // Calculate total balance a different way from GetBalance()
        // (GetBalance() sums up all unspent TxOuts)
        // getbalance and getbalance '*' should always return the same number.
        int64 nBalance = 0;
        for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
        {
            const CWalletTx& wtx = (*it).second;
            if (!wtx.IsFinal())
                continue;

            int64 allGeneratedImmature, allGeneratedMature, allFee;
            allGeneratedImmature = allGeneratedMature = allFee = 0;
            string strSentAccount;
            list<pair<CBitcoinAddress, int64> > listReceived;
            list<pair<CBitcoinAddress, int64> > listSent;
            wtx.GetAmounts(allGeneratedImmature, allGeneratedMature, listReceived, listSent, allFee, strSentAccount);
            if (wtx.GetDepthInMainChain() >= nMinDepth)
                BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress,int64)& r, listReceived)
                    nBalance += r.second;
            BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress,int64)& r, listSent)
                nBalance -= r.second;
            nBalance -= allFee;
            nBalance += allGeneratedMature;
        }
        return  ValueFromAmount(nBalance);
    }

    string strAccount = AccountFromValue(params[0]);

    int64 nBalance = GetAccountBalance(strAccount, nMinDepth);

    return ValueFromAmount(nBalance);
}


Value movecmd(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 5)
        throw runtime_error(
            "move <fromaccount> <toaccount> <amount> [minconf=1] [comment]\n"
            "Move from one account in your wallet to another.");

    string strFrom = AccountFromValue(params[0]);
    string strTo = AccountFromValue(params[1]);
    int64 nAmount = AmountFromValue(params[2]);
    if (params.size() > 3)
        // unused parameter, used to be nMinDepth, keep type-checking it though
        (void)params[3].get_int();
    string strComment;
    if (params.size() > 4)
        strComment = params[4].get_str();

    CWalletDB walletdb(pwalletMain->strWalletFile);
    walletdb.TxnBegin();

    int64 nNow = GetAdjustedTime();

    // Debit
    CAccountingEntry debit;
    debit.strAccount = strFrom;
    debit.nCreditDebit = -nAmount;
    debit.nTime = nNow;
    debit.strOtherAccount = strTo;
    debit.strComment = strComment;
    walletdb.WriteAccountingEntry(debit);

    // Credit
    CAccountingEntry credit;
    credit.strAccount = strTo;
    credit.nCreditDebit = nAmount;
    credit.nTime = nNow;
    credit.strOtherAccount = strFrom;
    credit.strComment = strComment;
    walletdb.WriteAccountingEntry(credit);

    walletdb.TxnCommit();

    return true;
}


Value sendfrom(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() < 3 || params.size() > 6))
        throw runtime_error(
            "sendfrom <fromaccount> <tolitecoinaddress> <amount> [minconf=1] [comment] [comment-to]\n"
            "<amount> is a real and is rounded to the nearest 0.00000001\n"
            "requires wallet passphrase to be set with walletpassphrase first");
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() < 3 || params.size() > 6))
        throw runtime_error(
            "sendfrom <fromaccount> <tolitecoinaddress> <amount> [minconf=1] [comment] [comment-to]\n"
            "<amount> is a real and is rounded to the nearest 0.00000001");

    string strAccount = AccountFromValue(params[0]);
    CBitcoinAddress address(params[1].get_str());
    if (!address.IsValid())
        throw JSONRPCError(-5, "Invalid litecoin address");
    int64 nAmount = AmountFromValue(params[2]);
    int nMinDepth = 1;
    if (params.size() > 3)
        nMinDepth = params[3].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (params.size() > 4 && params[4].type() != null_type && !params[4].get_str().empty())
        wtx.mapValue["comment"] = params[4].get_str();
    if (params.size() > 5 && params[5].type() != null_type && !params[5].get_str().empty())
        wtx.mapValue["to"]      = params[5].get_str();

    if (pwalletMain->IsLocked())
        throw JSONRPCError(-13, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    // Check funds
    int64 nBalance = GetAccountBalance(strAccount, nMinDepth);
    if (nAmount > nBalance)
        throw JSONRPCError(-6, "Account has insufficient funds");

    // Send
    string strError = pwalletMain->SendMoneyToBitcoinAddress(address, nAmount, wtx);
    if (strError != "")
        throw JSONRPCError(-4, strError);

    return wtx.GetHash().GetHex();
}


Value sendmany(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() < 2 || params.size() > 4))
        throw runtime_error(
            "sendmany <fromaccount> {address:amount,...} [minconf=1] [comment]\n"
            "amounts are double-precision floating point numbers\n"
            "requires wallet passphrase to be set with walletpassphrase first");
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() < 2 || params.size() > 4))
        throw runtime_error(
            "sendmany <fromaccount> {address:amount,...} [minconf=1] [comment]\n"
            "amounts are double-precision floating point numbers");

    string strAccount = AccountFromValue(params[0]);
    Object sendTo = params[1].get_obj();
    int nMinDepth = 1;
    if (params.size() > 2)
        nMinDepth = params[2].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (params.size() > 3 && params[3].type() != null_type && !params[3].get_str().empty())
        wtx.mapValue["comment"] = params[3].get_str();

    set<CBitcoinAddress> setAddress;
    vector<pair<CScript, int64> > vecSend;

    int64 totalAmount = 0;
    BOOST_FOREACH(const Pair& s, sendTo)
    {
        CBitcoinAddress address(s.name_);
        if (!address.IsValid())
            throw JSONRPCError(-5, string("Invalid litecoin address:")+s.name_);

        if (setAddress.count(address))
            throw JSONRPCError(-8, string("Invalid parameter, duplicated address: ")+s.name_);
        setAddress.insert(address);

        CScript scriptPubKey;
        scriptPubKey.SetBitcoinAddress(address);
        int64 nAmount = AmountFromValue(s.value_); 
        totalAmount += nAmount;

        vecSend.push_back(make_pair(scriptPubKey, nAmount));
    }

    if (pwalletMain->IsLocked())
        throw JSONRPCError(-13, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    // Check funds
    int64 nBalance = GetAccountBalance(strAccount, nMinDepth);
    if (totalAmount > nBalance)
        throw JSONRPCError(-6, "Account has insufficient funds");

    // Send
    CReserveKey keyChange(pwalletMain);
    int64 nFeeRequired = 0;
    bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired);
    if (!fCreated)
    {
        if (totalAmount + nFeeRequired > pwalletMain->GetBalance())
            throw JSONRPCError(-6, "Insufficient funds");
        throw JSONRPCError(-4, "Transaction creation failed");
    }
    if (!pwalletMain->CommitTransaction(wtx, keyChange))
        throw JSONRPCError(-4, "Transaction commit failed");

    return wtx.GetHash().GetHex();
}

Value addmultisigaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
    {
        string msg = "addmultisigaddress <nrequired> <'[\"key\",\"key\"]'> [account]\n"
            "Add a nrequired-to-sign multisignature address to the wallet\"\n"
            "each key is a bitcoin address or hex-encoded public key\n"
            "If [account] is specified, assign address to [account].";
        throw runtime_error(msg);
    }
    if (!fTestNet)
        throw runtime_error("addmultisigaddress available only when running -testnet\n");

    int nRequired = params[0].get_int();
    const Array& keys = params[1].get_array();
    string strAccount;
    if (params.size() > 2)
        strAccount = AccountFromValue(params[2]);

    // Gather public keys
    if (nRequired < 1 || keys.size() < nRequired)
        throw runtime_error(
            strprintf("wrong number of keys"
                      "(got %d, need at least %d)", keys.size(), nRequired));
    std::vector<CKey> pubkeys;
    pubkeys.resize(keys.size());
    for (int i = 0; i < keys.size(); i++)
    {
        const std::string& ks = keys[i].get_str();

        // Case 1: bitcoin address and we have full public key:
        CBitcoinAddress address(ks);
        if (address.IsValid())
        {
            if (address.IsScript())
                throw runtime_error(
                    strprintf("%s is a pay-to-script address",ks.c_str()));
            std::vector<unsigned char> vchPubKey;
            if (!pwalletMain->GetPubKey(address, vchPubKey))
                throw runtime_error(
                    strprintf("no full public key for address %s",ks.c_str()));
            if (vchPubKey.empty() || !pubkeys[i].SetPubKey(vchPubKey))
                throw runtime_error(" Invalid public key: "+ks);
        }

        // Case 2: hex public key
        else if (IsHex(ks))
        {
            vector<unsigned char> vchPubKey = ParseHex(ks);
            if (vchPubKey.empty() || !pubkeys[i].SetPubKey(vchPubKey))
                throw runtime_error(" Invalid public key: "+ks);
        }
        else
        {
            throw runtime_error(" Invalid public key: "+ks);
        }
    }

    // Construct using pay-to-script-hash:
    CScript inner;
    inner.SetMultisig(nRequired, pubkeys);

    uint160 scriptHash = Hash160(inner);
    CScript scriptPubKey;
    scriptPubKey.SetPayToScriptHash(inner);
    pwalletMain->AddCScript(inner);
    CBitcoinAddress address;
    address.SetScriptHash160(scriptHash);

    pwalletMain->SetAddressBookName(address, strAccount);
    return address.ToString();
}


struct tallyitem
{
    int64 nAmount;
    int nConf;
    tallyitem()
    {
        nAmount = 0;
        nConf = std::numeric_limits<int>::max();
    }
};

Value ListReceived(const Array& params, bool fByAccounts)
{
    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    // Whether to include empty accounts
    bool fIncludeEmpty = false;
    if (params.size() > 1)
        fIncludeEmpty = params[1].get_bool();

    // Tally
    map<CBitcoinAddress, tallyitem> mapTally;
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;

        if (wtx.IsCoinBase() || !wtx.IsFinal())
            continue;

        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < nMinDepth)
            continue;

        BOOST_FOREACH(const CTxOut& txout, wtx.vout)
        {
            CBitcoinAddress address;
            if (!ExtractAddress(txout.scriptPubKey, address) || !pwalletMain->HaveKey(address) || !address.IsValid())
                continue;

            tallyitem& item = mapTally[address];
            item.nAmount += txout.nValue;
            item.nConf = min(item.nConf, nDepth);
        }
    }

    // Reply
    Array ret;
    map<string, tallyitem> mapAccountTally;
    BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, string)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        const string& strAccount = item.second;
        map<CBitcoinAddress, tallyitem>::iterator it = mapTally.find(address);
        if (it == mapTally.end() && !fIncludeEmpty)
            continue;

        int64 nAmount = 0;
        int nConf = std::numeric_limits<int>::max();
        if (it != mapTally.end())
        {
            nAmount = (*it).second.nAmount;
            nConf = (*it).second.nConf;
        }

        if (fByAccounts)
        {
            tallyitem& item = mapAccountTally[strAccount];
            item.nAmount += nAmount;
            item.nConf = min(item.nConf, nConf);
        }
        else
        {
            Object obj;
            obj.push_back(Pair("address",       address.ToString()));
            obj.push_back(Pair("account",       strAccount));
            obj.push_back(Pair("amount",        ValueFromAmount(nAmount)));
            obj.push_back(Pair("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf)));
            ret.push_back(obj);
        }
    }

    if (fByAccounts)
    {
        for (map<string, tallyitem>::iterator it = mapAccountTally.begin(); it != mapAccountTally.end(); ++it)
        {
            int64 nAmount = (*it).second.nAmount;
            int nConf = (*it).second.nConf;
            Object obj;
            obj.push_back(Pair("account",       (*it).first));
            obj.push_back(Pair("amount",        ValueFromAmount(nAmount)));
            obj.push_back(Pair("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf)));
            ret.push_back(obj);
        }
    }

    return ret;
}

Value listreceivedbyaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "listreceivedbyaddress [minconf=1] [includeempty=false]\n"
            "[minconf] is the minimum number of confirmations before payments are included.\n"
            "[includeempty] whether to include addresses that haven't received any payments.\n"
            "Returns an array of objects containing:\n"
            "  \"address\" : receiving address\n"
            "  \"account\" : the account of the receiving address\n"
            "  \"amount\" : total amount received by the address\n"
            "  \"confirmations\" : number of confirmations of the most recent transaction included");

    return ListReceived(params, false);
}

Value listreceivedbyaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "listreceivedbyaccount [minconf=1] [includeempty=false]\n"
            "[minconf] is the minimum number of confirmations before payments are included.\n"
            "[includeempty] whether to include accounts that haven't received any payments.\n"
            "Returns an array of objects containing:\n"
            "  \"account\" : the account of the receiving addresses\n"
            "  \"amount\" : total amount received by addresses with this account\n"
            "  \"confirmations\" : number of confirmations of the most recent transaction included");

    return ListReceived(params, true);
}

void ListTransactions(const CWalletTx& wtx, const string& strAccount, int nMinDepth, bool fLong, Array& ret)
{
    int64 nGeneratedImmature, nGeneratedMature, nFee;
    string strSentAccount;
    list<pair<CBitcoinAddress, int64> > listReceived;
    list<pair<CBitcoinAddress, int64> > listSent;

    wtx.GetAmounts(nGeneratedImmature, nGeneratedMature, listReceived, listSent, nFee, strSentAccount);

    bool fAllAccounts = (strAccount == string("*"));

    // Generated blocks assigned to account ""
    if ((nGeneratedMature+nGeneratedImmature) != 0 && (fAllAccounts || strAccount == ""))
    {
        Object entry;
        entry.push_back(Pair("account", string("")));
        if (nGeneratedImmature)
        {
            entry.push_back(Pair("category", wtx.GetDepthInMainChain() ? "immature" : "orphan"));
            entry.push_back(Pair("amount", ValueFromAmount(nGeneratedImmature)));
        }
        else
        {
            entry.push_back(Pair("category", "generate"));
            entry.push_back(Pair("amount", ValueFromAmount(nGeneratedMature)));
        }
        if (fLong)
            WalletTxToJSON(wtx, entry);
        ret.push_back(entry);
    }

    // Sent
    if ((!listSent.empty() || nFee != 0) && (fAllAccounts || strAccount == strSentAccount))
    {
        BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, int64)& s, listSent)
        {
            Object entry;
            entry.push_back(Pair("account", strSentAccount));
            entry.push_back(Pair("address", s.first.ToString()));
            entry.push_back(Pair("category", "send"));
            entry.push_back(Pair("amount", ValueFromAmount(-s.second)));
            entry.push_back(Pair("fee", ValueFromAmount(-nFee)));
            if (fLong)
                WalletTxToJSON(wtx, entry);
            ret.push_back(entry);
        }
    }

    // Received
    if (listReceived.size() > 0 && wtx.GetDepthInMainChain() >= nMinDepth)
        BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, int64)& r, listReceived)
        {
            string account;
            if (pwalletMain->mapAddressBook.count(r.first))
                account = pwalletMain->mapAddressBook[r.first];
            if (fAllAccounts || (account == strAccount))
            {
                Object entry;
                entry.push_back(Pair("account", account));
                entry.push_back(Pair("address", r.first.ToString()));
                entry.push_back(Pair("category", "receive"));
                entry.push_back(Pair("amount", ValueFromAmount(r.second)));
                if (fLong)
                    WalletTxToJSON(wtx, entry);
                ret.push_back(entry);
            }
        }
}

void AcentryToJSON(const CAccountingEntry& acentry, const string& strAccount, Array& ret)
{
    bool fAllAccounts = (strAccount == string("*"));

    if (fAllAccounts || acentry.strAccount == strAccount)
    {
        Object entry;
        entry.push_back(Pair("account", acentry.strAccount));
        entry.push_back(Pair("category", "move"));
        entry.push_back(Pair("time", (boost::int64_t)acentry.nTime));
        entry.push_back(Pair("amount", ValueFromAmount(acentry.nCreditDebit)));
        entry.push_back(Pair("otheraccount", acentry.strOtherAccount));
        entry.push_back(Pair("comment", acentry.strComment));
        ret.push_back(entry);
    }
}

Value listtransactions(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
            "listtransactions [account] [count=10] [from=0]\n"
            "Returns up to [count] most recent transactions skipping the first [from] transactions for account [account].");

    string strAccount = "*";
    if (params.size() > 0)
        strAccount = params[0].get_str();
    int nCount = 10;
    if (params.size() > 1)
        nCount = params[1].get_int();
    int nFrom = 0;
    if (params.size() > 2)
        nFrom = params[2].get_int();

    Array ret;
    CWalletDB walletdb(pwalletMain->strWalletFile);

    // Firs: get all CWalletTx and CAccountingEntry into a sorted-by-time multimap:
    typedef pair<CWalletTx*, CAccountingEntry*> TxPair;
    typedef multimap<int64, TxPair > TxItems;
    TxItems txByTime;

    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        CWalletTx* wtx = &((*it).second);
        txByTime.insert(make_pair(wtx->GetTxTime(), TxPair(wtx, (CAccountingEntry*)0)));
    }
    list<CAccountingEntry> acentries;
    walletdb.ListAccountCreditDebit(strAccount, acentries);
    BOOST_FOREACH(CAccountingEntry& entry, acentries)
    {
        txByTime.insert(make_pair(entry.nTime, TxPair((CWalletTx*)0, &entry)));
    }

    // Now: iterate backwards until we have nCount items to return:
    TxItems::reverse_iterator it = txByTime.rbegin();
    if (txByTime.size() > nFrom) std::advance(it, nFrom);
    for (; it != txByTime.rend(); ++it)
    {
        CWalletTx *const pwtx = (*it).second.first;
        if (pwtx != 0)
            ListTransactions(*pwtx, strAccount, 0, true, ret);
        CAccountingEntry *const pacentry = (*it).second.second;
        if (pacentry != 0)
            AcentryToJSON(*pacentry, strAccount, ret);

        if (ret.size() >= nCount) break;
    }
    // ret is now newest to oldest
    
    // Make sure we return only last nCount items (sends-to-self might give us an extra):
    if (ret.size() > nCount)
    {
        Array::iterator last = ret.begin();
        std::advance(last, nCount);
        ret.erase(last, ret.end());
    }
    std::reverse(ret.begin(), ret.end()); // oldest to newest

    return ret;
}

Value listaccounts(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "listaccounts [minconf=1]\n"
            "Returns Object that has account names as keys, account balances as values.");

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    map<string, int64> mapAccountBalances;
    BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, string)& entry, pwalletMain->mapAddressBook) {
        if (pwalletMain->HaveKey(entry.first)) // This address belongs to me
            mapAccountBalances[entry.second] = 0;
    }

    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        int64 nGeneratedImmature, nGeneratedMature, nFee;
        string strSentAccount;
        list<pair<CBitcoinAddress, int64> > listReceived;
        list<pair<CBitcoinAddress, int64> > listSent;
        wtx.GetAmounts(nGeneratedImmature, nGeneratedMature, listReceived, listSent, nFee, strSentAccount);
        mapAccountBalances[strSentAccount] -= nFee;
        BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, int64)& s, listSent)
            mapAccountBalances[strSentAccount] -= s.second;
        if (wtx.GetDepthInMainChain() >= nMinDepth)
        {
            mapAccountBalances[""] += nGeneratedMature;
            BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, int64)& r, listReceived)
                if (pwalletMain->mapAddressBook.count(r.first))
                    mapAccountBalances[pwalletMain->mapAddressBook[r.first]] += r.second;
                else
                    mapAccountBalances[""] += r.second;
        }
    }

    list<CAccountingEntry> acentries;
    CWalletDB(pwalletMain->strWalletFile).ListAccountCreditDebit("*", acentries);
    BOOST_FOREACH(const CAccountingEntry& entry, acentries)
        mapAccountBalances[entry.strAccount] += entry.nCreditDebit;

    Object ret;
    BOOST_FOREACH(const PAIRTYPE(string, int64)& accountBalance, mapAccountBalances) {
        ret.push_back(Pair(accountBalance.first, ValueFromAmount(accountBalance.second)));
    }
    return ret;
}

Value listsinceblock(const Array& params, bool fHelp)
{
    if (fHelp)
        throw runtime_error(
            "listsinceblock [blockid] [target-confirmations]\n"
            "Get all transactions in blocks since block [blockid], or all transactions if omitted");

    CBlockIndex *pindex = NULL;
    int target_confirms = 1;

    if (params.size() > 0)
    {
        uint256 blockId = 0;

        blockId.SetHex(params[0].get_str());
        pindex = CBlockLocator(blockId).GetBlockIndex();
    }

    if (params.size() > 1)
    {
        target_confirms = params[1].get_int();

        if (target_confirms < 1)
            throw JSONRPCError(-8, "Invalid parameter");
    }

    int depth = pindex ? (1 + nBestHeight - pindex->nHeight) : -1;

    Array transactions;

    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); it++)
    {
        CWalletTx tx = (*it).second;

        if (depth == -1 || tx.GetDepthInMainChain() < depth)
            ListTransactions(tx, "*", 0, true, transactions);
    }

    uint256 lastblock;

    if (target_confirms == 1)
    {
        lastblock = hashBestChain;
    }
    else
    {
        int target_height = pindexBest->nHeight + 1 - target_confirms;

        CBlockIndex *block;
        for (block = pindexBest;
             block && block->nHeight > target_height;
             block = block->pprev)  { }

        lastblock = block ? block->GetBlockHash() : 0;
    }

    Object ret;
    ret.push_back(Pair("transactions", transactions));
    ret.push_back(Pair("lastblock", lastblock.GetHex()));

    return ret;
}

Value gettransaction(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "gettransaction <txid>\n"
            "Get detailed information about <txid>");

    uint256 hash;
    hash.SetHex(params[0].get_str());

    Object entry;

    if (!pwalletMain->mapWallet.count(hash))
        throw JSONRPCError(-5, "Invalid or non-wallet transaction id");
    const CWalletTx& wtx = pwalletMain->mapWallet[hash];

    int64 nCredit = wtx.GetCredit();
    int64 nDebit = wtx.GetDebit();
    int64 nNet = nCredit - nDebit;
    int64 nFee = (wtx.IsFromMe() ? wtx.GetValueOut() - nDebit : 0);

    entry.push_back(Pair("amount", ValueFromAmount(nNet - nFee)));
    if (wtx.IsFromMe())
        entry.push_back(Pair("fee", ValueFromAmount(nFee)));

    WalletTxToJSON(pwalletMain->mapWallet[hash], entry);

    Array details;
    ListTransactions(pwalletMain->mapWallet[hash], "*", 0, false, details);
    entry.push_back(Pair("details", details));

    return entry;
}


Value backupwallet(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "backupwallet <destination>\n"
            "Safely copies wallet.dat to destination, which can be a directory or a path with filename.");

    string strDest = params[0].get_str();
    BackupWallet(*pwalletMain, strDest);

    return Value::null;
}


Value keypoolrefill(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() > 0))
        throw runtime_error(
            "keypoolrefill\n"
            "Fills the keypool, requires wallet passphrase to be set.");
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() > 0))
        throw runtime_error(
            "keypoolrefill\n"
            "Fills the keypool.");

    if (pwalletMain->IsLocked())
        throw JSONRPCError(-13, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    pwalletMain->TopUpKeyPool();

    if (pwalletMain->GetKeyPoolSize() < GetArg("-keypool", 100))
        throw JSONRPCError(-4, "Error refreshing keypool.");

    return Value::null;
}


void ThreadTopUpKeyPool(void* parg)
{
    pwalletMain->TopUpKeyPool();
}

void ThreadCleanWalletPassphrase(void* parg)
{
    int64 nMyWakeTime = GetTimeMillis() + *((int64*)parg) * 1000;

    ENTER_CRITICAL_SECTION(cs_nWalletUnlockTime);

    if (nWalletUnlockTime == 0)
    {
        nWalletUnlockTime = nMyWakeTime;

        do
        {
            if (nWalletUnlockTime==0)
                break;
            int64 nToSleep = nWalletUnlockTime - GetTimeMillis();
            if (nToSleep <= 0)
                break;

            LEAVE_CRITICAL_SECTION(cs_nWalletUnlockTime);
            Sleep(nToSleep);
            ENTER_CRITICAL_SECTION(cs_nWalletUnlockTime);

        } while(1);

        if (nWalletUnlockTime)
        {
            nWalletUnlockTime = 0;
            pwalletMain->Lock();
        }
    }
    else
    {
        if (nWalletUnlockTime < nMyWakeTime)
            nWalletUnlockTime = nMyWakeTime;
    }

    LEAVE_CRITICAL_SECTION(cs_nWalletUnlockTime);

    delete (int64*)parg;
}

Value walletpassphrase(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 2))
        throw runtime_error(
            "walletpassphrase <passphrase> <timeout>\n"
            "Stores the wallet decryption key in memory for <timeout> seconds.");
    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(-15, "Error: running with an unencrypted wallet, but walletpassphrase was called.");

    if (!pwalletMain->IsLocked())
        throw JSONRPCError(-17, "Error: Wallet is already unlocked.");

    // Note that the walletpassphrase is stored in params[0] which is not mlock()ed
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    strWalletPass = params[0].get_str().c_str();

    if (strWalletPass.length() > 0)
    {
        if (!pwalletMain->Unlock(strWalletPass))
            throw JSONRPCError(-14, "Error: The wallet passphrase entered was incorrect.");
    }
    else
        throw runtime_error(
            "walletpassphrase <passphrase> <timeout>\n"
            "Stores the wallet decryption key in memory for <timeout> seconds.");

    CreateThread(ThreadTopUpKeyPool, NULL);
    int64* pnSleepTime = new int64(params[1].get_int64());
    CreateThread(ThreadCleanWalletPassphrase, pnSleepTime);

    return Value::null;
}


Value walletpassphrasechange(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 2))
        throw runtime_error(
            "walletpassphrasechange <oldpassphrase> <newpassphrase>\n"
            "Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.");
    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(-15, "Error: running with an unencrypted wallet, but walletpassphrasechange was called.");

    // TODO: get rid of these .c_str() calls by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    SecureString strOldWalletPass;
    strOldWalletPass.reserve(100);
    strOldWalletPass = params[0].get_str().c_str();

    SecureString strNewWalletPass;
    strNewWalletPass.reserve(100);
    strNewWalletPass = params[1].get_str().c_str();

    if (strOldWalletPass.length() < 1 || strNewWalletPass.length() < 1)
        throw runtime_error(
            "walletpassphrasechange <oldpassphrase> <newpassphrase>\n"
            "Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.");

    if (!pwalletMain->ChangeWalletPassphrase(strOldWalletPass, strNewWalletPass))
        throw JSONRPCError(-14, "Error: The wallet passphrase entered was incorrect.");

    return Value::null;
}


Value walletlock(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 0))
        throw runtime_error(
            "walletlock\n"
            "Removes the wallet encryption key from memory, locking the wallet.\n"
            "After calling this method, you will need to call walletpassphrase again\n"
            "before being able to call any methods which require the wallet to be unlocked.");
    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(-15, "Error: running with an unencrypted wallet, but walletlock was called.");

    CRITICAL_BLOCK(cs_nWalletUnlockTime)
    {
        pwalletMain->Lock();
        nWalletUnlockTime = 0;
    }

    return Value::null;
}


Value encryptwallet(const Array& params, bool fHelp)
{
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() != 1))
        throw runtime_error(
            "encryptwallet <passphrase>\n"
            "Encrypts the wallet with <passphrase>.");
    if (fHelp)
        return true;
    if (pwalletMain->IsCrypted())
        throw JSONRPCError(-15, "Error: running with an encrypted wallet, but encryptwallet was called.");

#ifdef QT_GUI
    // shutting down via RPC while the GUI is running does not work (yet):
    throw runtime_error("Not Yet Implemented: use GUI to encrypt wallet, not RPC command");
#endif

    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = params[0].get_str().c_str();

    if (strWalletPass.length() < 1)
        throw runtime_error(
            "encryptwallet <passphrase>\n"
            "Encrypts the wallet with <passphrase>.");

    if (!pwalletMain->EncryptWallet(strWalletPass))
        throw JSONRPCError(-16, "Error: Failed to encrypt the wallet.");

    // BDB seems to have a bad habit of writing old data into
    // slack space in .dat files; that is bad if the old data is
    // unencrypted private keys.  So:
    CreateThread(Shutdown, NULL);
    return "wallet encrypted; litecoin server stopping, restart to run with encrypted wallet";
}


Value validateaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "validateaddress <litecoinaddress>\n"
            "Return information about <litecoinaddress>.");

    CBitcoinAddress address(params[0].get_str());
    bool isValid = address.IsValid();

    Object ret;
    ret.push_back(Pair("isvalid", isValid));
    if (isValid)
    {
        // Call Hash160ToAddress() so we always return current ADDRESSVERSION
        // version of the address:
        string currentAddress = address.ToString();
        ret.push_back(Pair("address", currentAddress));
        if (pwalletMain->HaveKey(address))
        {
            ret.push_back(Pair("ismine", true));
            std::vector<unsigned char> vchPubKey;
            pwalletMain->GetPubKey(address, vchPubKey);
            ret.push_back(Pair("pubkey", HexStr(vchPubKey)));
            CKey key;
            key.SetPubKey(vchPubKey);
            ret.push_back(Pair("iscompressed", key.IsCompressed()));
        }
        else if (pwalletMain->HaveCScript(address.GetHash160()))
        {
            ret.push_back(Pair("isscript", true));
            CScript subscript;
            pwalletMain->GetCScript(address.GetHash160(), subscript);
            ret.push_back(Pair("ismine", ::IsMine(*pwalletMain, subscript)));
            std::vector<CBitcoinAddress> addresses;
            txnouttype whichType;
            int nRequired;
            ExtractAddresses(subscript, whichType, addresses, nRequired);
            ret.push_back(Pair("script", GetTxnOutputType(whichType)));
            Array a;
            BOOST_FOREACH(const CBitcoinAddress& addr, addresses)
                a.push_back(addr.ToString());
            ret.push_back(Pair("addresses", a));
            if (whichType == TX_MULTISIG)
                ret.push_back(Pair("sigsrequired", nRequired));
        }
        else
            ret.push_back(Pair("ismine", false));
        if (pwalletMain->mapAddressBook.count(address))
            ret.push_back(Pair("account", pwalletMain->mapAddressBook[address]));
    }
    return ret;
}

Value getworkex(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "getworkex [data, coinbase]\n"
            "If [data, coinbase] is not specified, returns extended work data.\n"
        );

    if (vNodes.empty())
        throw JSONRPCError(-9, "Litecoin is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(-10, "Litecoin is downloading blocks...");

    typedef map<uint256, pair<CBlock*, CScript> > mapNewBlock_t;
    static mapNewBlock_t mapNewBlock;
    static vector<CBlock*> vNewBlock;
    static CReserveKey reservekey(pwalletMain);

    if (params.size() == 0)
    {
        // Update block
        static unsigned int nTransactionsUpdatedLast;
        static CBlockIndex* pindexPrev;
        static int64 nStart;
        static CBlock* pblock;
        if (pindexPrev != pindexBest ||
            (nTransactionsUpdated != nTransactionsUpdatedLast && GetTime() - nStart > 60))
        {
            if (pindexPrev != pindexBest)
            {
                // Deallocate old blocks since they're obsolete now
                mapNewBlock.clear();
                BOOST_FOREACH(CBlock* pblock, vNewBlock)
                    delete pblock;
                vNewBlock.clear();
            }
            nTransactionsUpdatedLast = nTransactionsUpdated;
            pindexPrev = pindexBest;
            nStart = GetTime();

            // Create new block
            pblock = CreateNewBlock(reservekey);
            if (!pblock)
                throw JSONRPCError(-7, "Out of memory");
            vNewBlock.push_back(pblock);
        }

        // Update nTime
        pblock->nTime = max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());
        pblock->nNonce = 0;

        // Update nExtraNonce
        static unsigned int nExtraNonce = 0;
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        // Save
        mapNewBlock[pblock->hashMerkleRoot] = make_pair(pblock, pblock->vtx[0].vin[0].scriptSig);

        // Prebuild hash buffers
        char pmidstate[32];
        char pdata[128];
        char phash1[64];
        FormatHashBuffers(pblock, pmidstate, pdata, phash1);

        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

        CTransaction coinbaseTx = pblock->vtx[0];
        std::vector<uint256> merkle = pblock->GetMerkleBranch(0);

        Object result;
        result.push_back(Pair("data",     HexStr(BEGIN(pdata), END(pdata))));
        result.push_back(Pair("target",   HexStr(BEGIN(hashTarget), END(hashTarget))));

        CDataStream ssTx;
        ssTx << coinbaseTx;
        result.push_back(Pair("coinbase", HexStr(ssTx.begin(), ssTx.end())));

        Array merkle_arr;
        printf("DEBUG: merkle size %i\n", merkle.size());

        BOOST_FOREACH(uint256 merkleh, merkle) {
            printf("%s\n", merkleh.ToString().c_str());
            merkle_arr.push_back(HexStr(BEGIN(merkleh), END(merkleh)));
        }

        result.push_back(Pair("merkle", merkle_arr));


        return result;
    }
    else
    {
        // Parse parameters
        vector<unsigned char> vchData = ParseHex(params[0].get_str());
        vector<unsigned char> coinbase;

        if(params.size() == 2)
            coinbase = ParseHex(params[1].get_str());

        if (vchData.size() != 128)
            throw JSONRPCError(-8, "Invalid parameter");

        CBlock* pdata = (CBlock*)&vchData[0];

        // Byte reverse
        for (int i = 0; i < 128/4; i++)
            ((unsigned int*)pdata)[i] = ByteReverse(((unsigned int*)pdata)[i]);

        // Get saved block
        if (!mapNewBlock.count(pdata->hashMerkleRoot))
            return false;
        CBlock* pblock = mapNewBlock[pdata->hashMerkleRoot].first;

        pblock->nTime = pdata->nTime;
        pblock->nNonce = pdata->nNonce;

        if(coinbase.size() == 0)
            pblock->vtx[0].vin[0].scriptSig = mapNewBlock[pdata->hashMerkleRoot].second;
        else
            CDataStream(coinbase) >> pblock->vtx[0]; // FIXME - HACK!

        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        return CheckWork(pblock, *pwalletMain, reservekey);
    }
}

Value getwork(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getwork [data]\n"
            "If [data] is not specified, returns formatted hash data to work on:\n"
            "  \"midstate\" : precomputed hash state after hashing the first half of the data (DEPRECATED)\n" // deprecated
            "  \"data\" : block data\n"
            "  \"hash1\" : formatted hash buffer for second hash (DEPRECATED)\n" // deprecated
            "  \"target\" : little endian hash target\n"
            "If [data] is specified, tries to solve the block and returns true if it was successful.");

    if (vNodes.empty())
        throw JSONRPCError(-9, "Litecoin is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(-10, "Litecoin is downloading blocks...");

    typedef map<uint256, pair<CBlock*, CScript> > mapNewBlock_t;
    static mapNewBlock_t mapNewBlock;
    static vector<CBlock*> vNewBlock;
    static CReserveKey reservekey(pwalletMain);

    if (params.size() == 0)
    {
        // Update block
        static unsigned int nTransactionsUpdatedLast;
        static CBlockIndex* pindexPrev;
        static int64 nStart;
        static CBlock* pblock;
        if (pindexPrev != pindexBest ||
            (nTransactionsUpdated != nTransactionsUpdatedLast && GetTime() - nStart > 60))
        {
            if (pindexPrev != pindexBest)
            {
                // Deallocate old blocks since they're obsolete now
                mapNewBlock.clear();
                BOOST_FOREACH(CBlock* pblock, vNewBlock)
                    delete pblock;
                vNewBlock.clear();
            }
            nTransactionsUpdatedLast = nTransactionsUpdated;
            pindexPrev = pindexBest;
            nStart = GetTime();

            // Create new block
            pblock = CreateNewBlock(reservekey);
            if (!pblock)
                throw JSONRPCError(-7, "Out of memory");
            vNewBlock.push_back(pblock);
        }

        // Update nTime
        pblock->UpdateTime(pindexPrev);
        pblock->nNonce = 0;

        // Update nExtraNonce
        static unsigned int nExtraNonce = 0;
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        // Save
        mapNewBlock[pblock->hashMerkleRoot] = make_pair(pblock, pblock->vtx[0].vin[0].scriptSig);

        // Prebuild hash buffers
        char pmidstate[32];
        char pdata[128];
        char phash1[64];
        FormatHashBuffers(pblock, pmidstate, pdata, phash1);

        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

        Object result;
        result.push_back(Pair("midstate", HexStr(BEGIN(pmidstate), END(pmidstate)))); // deprecated
        result.push_back(Pair("data",     HexStr(BEGIN(pdata), END(pdata))));
        result.push_back(Pair("hash1",    HexStr(BEGIN(phash1), END(phash1)))); // deprecated
        result.push_back(Pair("target",   HexStr(BEGIN(hashTarget), END(hashTarget))));
        return result;
    }
    else
    {
        // Parse parameters
        vector<unsigned char> vchData = ParseHex(params[0].get_str());
        if (vchData.size() != 128)
            throw JSONRPCError(-8, "Invalid parameter");
        CBlock* pdata = (CBlock*)&vchData[0];

        // Byte reverse
        for (int i = 0; i < 128/4; i++)
            ((unsigned int*)pdata)[i] = ByteReverse(((unsigned int*)pdata)[i]);

        // Get saved block
        if (!mapNewBlock.count(pdata->hashMerkleRoot))
            return false;
        CBlock* pblock = mapNewBlock[pdata->hashMerkleRoot].first;

        pblock->nTime = pdata->nTime;
        pblock->nNonce = pdata->nNonce;
        pblock->vtx[0].vin[0].scriptSig = mapNewBlock[pdata->hashMerkleRoot].second;
        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        return CheckWork(pblock, *pwalletMain, reservekey);
    }
}


Value getmemorypool(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getmemorypool [data]\n"
            "If [data] is not specified, returns data needed to construct a block to work on:\n"
            "  \"version\" : block version\n"
            "  \"previousblockhash\" : hash of current highest block\n"
            "  \"transactions\" : contents of non-coinbase transactions that should be included in the next block\n"
            "  \"coinbasevalue\" : maximum allowable input to coinbase transaction, including the generation award and transaction fees\n"
            "  \"coinbaseflags\" : data that should be included in coinbase so support for new features can be judged\n"
            "  \"time\" : timestamp appropriate for next block\n"
            "  \"mintime\" : minimum timestamp appropriate for next block\n"
            "  \"curtime\" : current timestamp\n"
            "  \"bits\" : compressed target of next block\n"
            "If [data] is specified, tries to solve the block and returns true if it was successful.");

    if (params.size() == 0)
    {
        if (vNodes.empty())
            throw JSONRPCError(-9, "Litecoin is not connected!");

        if (IsInitialBlockDownload())
            throw JSONRPCError(-10, "Litecoin is downloading blocks...");

        static CReserveKey reservekey(pwalletMain);

        // Update block
        static unsigned int nTransactionsUpdatedLast;
        static CBlockIndex* pindexPrev;
        static int64 nStart;
        static CBlock* pblock;
        if (pindexPrev != pindexBest ||
            (nTransactionsUpdated != nTransactionsUpdatedLast && GetTime() - nStart > 5))
        {
            nTransactionsUpdatedLast = nTransactionsUpdated;
            pindexPrev = pindexBest;
            nStart = GetTime();

            // Create new block
            if(pblock)
                delete pblock;
            pblock = CreateNewBlock(reservekey);
            if (!pblock)
                throw JSONRPCError(-7, "Out of memory");
        }

        // Update nTime
        pblock->UpdateTime(pindexPrev);
        pblock->nNonce = 0;

        Array transactions;
        BOOST_FOREACH(CTransaction tx, pblock->vtx) {
            if(tx.IsCoinBase())
                continue;

            CDataStream ssTx;
            ssTx << tx;

            transactions.push_back(HexStr(ssTx.begin(), ssTx.end()));
        }

        Object result;
        result.push_back(Pair("version", pblock->nVersion));
        result.push_back(Pair("previousblockhash", pblock->hashPrevBlock.GetHex()));
        result.push_back(Pair("transactions", transactions));
        result.push_back(Pair("coinbasevalue", (int64_t)pblock->vtx[0].vout[0].nValue));
        result.push_back(Pair("coinbaseflags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end())));
        result.push_back(Pair("time", (int64_t)pblock->nTime));
        result.push_back(Pair("mintime", (int64_t)pindexPrev->GetMedianTimePast()+1));
        result.push_back(Pair("curtime", (int64_t)GetAdjustedTime()));
        result.push_back(Pair("bits", HexBits(pblock->nBits)));

        return result;
    }
    else
    {
        // Parse parameters
        CDataStream ssBlock(ParseHex(params[0].get_str()));
        CBlock pblock;
        ssBlock >> pblock;

        return ProcessBlock(NULL, &pblock);
    }
}

Value getblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash <index>\n"
            "Returns hash of block in best-block-chain at <index>.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;
    return pblockindex->phashBlock->GetHex();
}

Value getblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblock <hash>\n"
            "Returns details of a block with given block-hash.");

    std::string strHash = params[0].get_str();
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(-5, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex);
}











//
// Call Table
//

pair<string, rpcfn_type> pCallTable[] =
{
    make_pair("help",                   &help),
    make_pair("stop",                   &stop),
    make_pair("getblockcount",          &getblockcount),
    make_pair("getblocknumber",         &getblocknumber),
    make_pair("getconnectioncount",     &getconnectioncount),
    make_pair("getdifficulty",          &getdifficulty),
    make_pair("getnetworkhashps",       &getnetworkhashps),
    make_pair("getgenerate",            &getgenerate),
    make_pair("setgenerate",            &setgenerate),
    make_pair("gethashespersec",        &gethashespersec),
    make_pair("getinfo",                &getinfo),
    make_pair("getmininginfo",          &getmininginfo),
    make_pair("getnewaddress",          &getnewaddress),
    make_pair("getaccountaddress",      &getaccountaddress),
    make_pair("setaccount",             &setaccount),
    make_pair("getaccount",             &getaccount),
    make_pair("getaddressesbyaccount",  &getaddressesbyaccount),
    make_pair("sendtoaddress",          &sendtoaddress),
    make_pair("getreceivedbyaddress",   &getreceivedbyaddress),
    make_pair("getreceivedbyaccount",   &getreceivedbyaccount),
    make_pair("listreceivedbyaddress",  &listreceivedbyaddress),
    make_pair("listreceivedbyaccount",  &listreceivedbyaccount),
    make_pair("backupwallet",           &backupwallet),
    make_pair("keypoolrefill",          &keypoolrefill),
    make_pair("walletpassphrase",       &walletpassphrase),
    make_pair("walletpassphrasechange", &walletpassphrasechange),
    make_pair("walletlock",             &walletlock),
    make_pair("encryptwallet",          &encryptwallet),
    make_pair("validateaddress",        &validateaddress),
    make_pair("getbalance",             &getbalance),
    make_pair("move",                   &movecmd),
    make_pair("sendfrom",               &sendfrom),
    make_pair("sendmany",               &sendmany),
    make_pair("addmultisigaddress",     &addmultisigaddress),
    make_pair("getblock",               &getblock),
    make_pair("getblockhash",           &getblockhash),
    make_pair("gettransaction",         &gettransaction),
    make_pair("listtransactions",       &listtransactions),
    make_pair("signmessage",            &signmessage),
    make_pair("verifymessage",          &verifymessage),
    make_pair("getwork",                &getwork),
    make_pair("getworkex",                &getworkex),
    make_pair("listaccounts",           &listaccounts),
    make_pair("settxfee",               &settxfee),
    make_pair("setmininput",            &setmininput),
    make_pair("getmemorypool",          &getmemorypool),
    make_pair("listsinceblock",         &listsinceblock),
    make_pair("dumpprivkey",            &dumpprivkey),
    make_pair("importprivkey",          &importprivkey)
};
map<string, rpcfn_type> mapCallTable(pCallTable, pCallTable + sizeof(pCallTable)/sizeof(pCallTable[0]));

string pAllowInSafeMode[] =
{
    "help",
    "stop",
    "getblockcount",
    "getblocknumber",  // deprecated
    "getconnectioncount",
    "getdifficulty",
    "getnetworkhashps",
    "getgenerate",
    "setgenerate",
    "gethashespersec",
    "getinfo",
    "getmininginfo",
    "getnewaddress",
    "getaccountaddress",
    "getaccount",
    "getaddressesbyaccount",
    "backupwallet",
    "keypoolrefill",
    "walletpassphrase",
    "walletlock",
    "validateaddress",
    "getwork",
	"getworkex",
    "getmemorypool",
};
set<string> setAllowInSafeMode(pAllowInSafeMode, pAllowInSafeMode + sizeof(pAllowInSafeMode)/sizeof(pAllowInSafeMode[0]));




//
// HTTP protocol
//
// This ain't Apache.  We're just using HTTP header for the length field
// and to be compatible with other JSON-RPC implementations.
//

string HTTPPost(const string& strMsg, const map<string,string>& mapRequestHeaders)
{
    ostringstream s;
    s << "POST / HTTP/1.1\r\n"
      << "User-Agent: litecoin-json-rpc/" << FormatFullVersion() << "\r\n"
      << "Host: 127.0.0.1\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << strMsg.size() << "\r\n"
      << "Connection: close\r\n"
      << "Accept: application/json\r\n";
    BOOST_FOREACH(const PAIRTYPE(string, string)& item, mapRequestHeaders)
        s << item.first << ": " << item.second << "\r\n";
    s << "\r\n" << strMsg;

    return s.str();
}

string rfc1123Time()
{
    char buffer[64];
    time_t now;
    time(&now);
    struct tm* now_gmt = gmtime(&now);
    string locale(setlocale(LC_TIME, NULL));
    setlocale(LC_TIME, "C"); // we want posix (aka "C") weekday/month strings
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S +0000", now_gmt);
    setlocale(LC_TIME, locale.c_str());
    return string(buffer);
}

static string HTTPReply(int nStatus, const string& strMsg)
{
    if (nStatus == 401)
        return strprintf("HTTP/1.0 401 Authorization Required\r\n"
            "Date: %s\r\n"
            "Server: litecoin-json-rpc/%s\r\n"
            "WWW-Authenticate: Basic realm=\"jsonrpc\"\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 296\r\n"
            "\r\n"
            "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\r\n"
            "\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\r\n"
            "<HTML>\r\n"
            "<HEAD>\r\n"
            "<TITLE>Error</TITLE>\r\n"
            "<META HTTP-EQUIV='Content-Type' CONTENT='text/html; charset=ISO-8859-1'>\r\n"
            "</HEAD>\r\n"
            "<BODY><H1>401 Unauthorized.</H1></BODY>\r\n"
            "</HTML>\r\n", rfc1123Time().c_str(), FormatFullVersion().c_str());
    const char *cStatus;
         if (nStatus == 200) cStatus = "OK";
    else if (nStatus == 400) cStatus = "Bad Request";
    else if (nStatus == 403) cStatus = "Forbidden";
    else if (nStatus == 404) cStatus = "Not Found";
    else if (nStatus == 500) cStatus = "Internal Server Error";
    else cStatus = "";
    return strprintf(
            "HTTP/1.1 %d %s\r\n"
            "Date: %s\r\n"
            "Connection: close\r\n"
            "Content-Length: %d\r\n"
            "Content-Type: application/json\r\n"
            "Server: litecoin-json-rpc/%s\r\n"
            "\r\n"
            "%s",
        nStatus,
        cStatus,
        rfc1123Time().c_str(),
        strMsg.size(),
        FormatFullVersion().c_str(),
        strMsg.c_str());
}

int ReadHTTPStatus(std::basic_istream<char>& stream)
{
    string str;
    getline(stream, str);
    vector<string> vWords;
    boost::split(vWords, str, boost::is_any_of(" "));
    if (vWords.size() < 2)
        return 500;
    return atoi(vWords[1].c_str());
}

int ReadHTTPHeader(std::basic_istream<char>& stream, map<string, string>& mapHeadersRet)
{
    int nLen = 0;
    loop
    {
        string str;
        std::getline(stream, str);
        if (str.empty() || str == "\r")
            break;
        string::size_type nColon = str.find(":");
        if (nColon != string::npos)
        {
            string strHeader = str.substr(0, nColon);
            boost::trim(strHeader);
            boost::to_lower(strHeader);
            string strValue = str.substr(nColon+1);
            boost::trim(strValue);
            mapHeadersRet[strHeader] = strValue;
            if (strHeader == "content-length")
                nLen = atoi(strValue.c_str());
        }
    }
    return nLen;
}

int ReadHTTP(std::basic_istream<char>& stream, map<string, string>& mapHeadersRet, string& strMessageRet)
{
    mapHeadersRet.clear();
    strMessageRet = "";

    // Read status
    int nStatus = ReadHTTPStatus(stream);

    // Read header
    int nLen = ReadHTTPHeader(stream, mapHeadersRet);
    if (nLen < 0 || nLen > MAX_SIZE)
        return 500;

    // Read message
    if (nLen > 0)
    {
        vector<char> vch(nLen);
        stream.read(&vch[0], nLen);
        strMessageRet = string(vch.begin(), vch.end());
    }

    return nStatus;
}

bool HTTPAuthorized(map<string, string>& mapHeaders)
{
    string strAuth = mapHeaders["authorization"];
    if (strAuth.substr(0,6) != "Basic ")
        return false;
    string strUserPass64 = strAuth.substr(6); boost::trim(strUserPass64);
    string strUserPass = DecodeBase64(strUserPass64);
    return strUserPass == strRPCUserColonPass;
}

//
// JSON-RPC protocol.  Litecoin speaks version 1.0 for maximum compatibility,
// but uses JSON-RPC 1.1/2.0 standards for parts of the 1.0 standard that were
// unspecified (HTTP errors and contents of 'error').
//
// 1.0 spec: http://json-rpc.org/wiki/specification
// 1.2 spec: http://groups.google.com/group/json-rpc/web/json-rpc-over-http
// http://www.codeproject.com/KB/recipes/JSON_Spirit.aspx
//

string JSONRPCRequest(const string& strMethod, const Array& params, const Value& id)
{
    Object request;
    request.push_back(Pair("method", strMethod));
    request.push_back(Pair("params", params));
    request.push_back(Pair("id", id));
    return write_string(Value(request), false) + "\n";
}

string JSONRPCReply(const Value& result, const Value& error, const Value& id)
{
    Object reply;
    if (error.type() != null_type)
        reply.push_back(Pair("result", Value::null));
    else
        reply.push_back(Pair("result", result));
    reply.push_back(Pair("error", error));
    reply.push_back(Pair("id", id));
    return write_string(Value(reply), false) + "\n";
}

void ErrorReply(std::ostream& stream, const Object& objError, const Value& id)
{
    // Send error reply from json-rpc error object
    int nStatus = 500;
    int code = find_value(objError, "code").get_int();
    if (code == -32600) nStatus = 400;
    else if (code == -32601) nStatus = 404;
    string strReply = JSONRPCReply(Value::null, objError, id);
    stream << HTTPReply(nStatus, strReply) << std::flush;
}

bool ClientAllowed(const string& strAddress)
{
    if (strAddress == asio::ip::address_v4::loopback().to_string())
        return true;
    const vector<string>& vAllow = mapMultiArgs["-rpcallowip"];
    BOOST_FOREACH(string strAllow, vAllow)
        if (WildcardMatch(strAddress, strAllow))
            return true;
    return false;
}

#ifdef USE_SSL
//
// IOStream device that speaks SSL but can also speak non-SSL
//
class SSLIOStreamDevice : public iostreams::device<iostreams::bidirectional> {
public:
    SSLIOStreamDevice(SSLStream &streamIn, bool fUseSSLIn) : stream(streamIn)
    {
        fUseSSL = fUseSSLIn;
        fNeedHandshake = fUseSSLIn;
    }

    void handshake(ssl::stream_base::handshake_type role)
    {
        if (!fNeedHandshake) return;
        fNeedHandshake = false;
        stream.handshake(role);
    }
    std::streamsize read(char* s, std::streamsize n)
    {
        handshake(ssl::stream_base::server); // HTTPS servers read first
        if (fUseSSL) return stream.read_some(asio::buffer(s, n));
        return stream.next_layer().read_some(asio::buffer(s, n));
    }
    std::streamsize write(const char* s, std::streamsize n)
    {
        handshake(ssl::stream_base::client); // HTTPS clients write first
        if (fUseSSL) return asio::write(stream, asio::buffer(s, n));
        return asio::write(stream.next_layer(), asio::buffer(s, n));
    }
    bool connect(const std::string& server, const std::string& port)
    {
        ip::tcp::resolver resolver(stream.get_io_service());
        ip::tcp::resolver::query query(server.c_str(), port.c_str());
        ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        ip::tcp::resolver::iterator end;
        boost::system::error_code error = asio::error::host_not_found;
        while (error && endpoint_iterator != end)
        {
            stream.lowest_layer().close();
            stream.lowest_layer().connect(*endpoint_iterator++, error);
        }
        if (error)
            return false;
        return true;
    }

private:
    bool fNeedHandshake;
    bool fUseSSL;
    SSLStream& stream;
};
#endif

void ThreadRPCServer(void* parg)
{
    IMPLEMENT_RANDOMIZE_STACK(ThreadRPCServer(parg));
    try
    {
        vnThreadsRunning[THREAD_RPCSERVER]++;
        ThreadRPCServer2(parg);
        vnThreadsRunning[THREAD_RPCSERVER]--;
    }
    catch (std::exception& e) {
        vnThreadsRunning[THREAD_RPCSERVER]--;
        PrintException(&e, "ThreadRPCServer()");
    } catch (...) {
        vnThreadsRunning[THREAD_RPCSERVER]--;
        PrintException(NULL, "ThreadRPCServer()");
    }
    printf("ThreadRPCServer exiting\n");
}

void ThreadRPCServer2(void* parg)
{
    printf("ThreadRPCServer started\n");

    strRPCUserColonPass = mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"];
    if (mapArgs["-rpcpassword"] == "")
    {
        unsigned char rand_pwd[32];
        RAND_bytes(rand_pwd, 32);
        string strWhatAmI = "To use litecoind";
        if (mapArgs.count("-server"))
            strWhatAmI = strprintf(_("To use the %s option"), "\"-server\"");
        else if (mapArgs.count("-daemon"))
            strWhatAmI = strprintf(_("To use the %s option"), "\"-daemon\"");
        ::error(
            _("%s, you must set a rpcpassword in the configuration file:\n %s\n"
              "It is recommended you use the following random password:\n"
              "rpcuser=bitcoinrpc\n"
              "rpcpassword=%s\n"
              "(you do not need to remember this password)\n"
              "If the file does not exist, create it with owner-readable-only file permissions.\n"),
                strWhatAmI.c_str(),
                GetConfigFile().c_str(),
                EncodeBase58(&rand_pwd[0],&rand_pwd[0]+32).c_str());
#ifndef QT_GUI
        CreateThread(Shutdown, NULL);
#endif
        return;
    }

    bool fUseSSL = GetBoolArg("-rpcssl");
    asio::ip::address bindAddress = mapArgs.count("-rpcallowip") ? asio::ip::address_v4::any() : asio::ip::address_v4::loopback();

    asio::io_service io_service;
    ip::tcp::endpoint endpoint(bindAddress, GetArg("-rpcport", 9332));
    ip::tcp::acceptor acceptor(io_service, endpoint);

    acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

#ifdef USE_SSL
    ssl::context context(io_service, ssl::context::sslv23);
    if (fUseSSL)
    {
        context.set_options(ssl::context::no_sslv2);
        filesystem::path certfile = GetArg("-rpcsslcertificatechainfile", "server.cert");
        if (!certfile.is_complete()) certfile = filesystem::path(GetDataDir()) / certfile;
        if (filesystem::exists(certfile)) context.use_certificate_chain_file(certfile.string().c_str());
        else printf("ThreadRPCServer ERROR: missing server certificate file %s\n", certfile.string().c_str());
        filesystem::path pkfile = GetArg("-rpcsslprivatekeyfile", "server.pem");
        if (!pkfile.is_complete()) pkfile = filesystem::path(GetDataDir()) / pkfile;
        if (filesystem::exists(pkfile)) context.use_private_key_file(pkfile.string().c_str(), ssl::context::pem);
        else printf("ThreadRPCServer ERROR: missing server private key file %s\n", pkfile.string().c_str());

        string ciphers = GetArg("-rpcsslciphers",
                                         "TLSv1+HIGH:!SSLv2:!aNULL:!eNULL:!AH:!3DES:@STRENGTH");
        SSL_CTX_set_cipher_list(context.impl(), ciphers.c_str());
    }
#else
    if (fUseSSL)
        throw runtime_error("-rpcssl=1, but litecoin compiled without full openssl libraries.");
#endif

    loop
    {
        // Accept connection
#ifdef USE_SSL
        SSLStream sslStream(io_service, context);
        SSLIOStreamDevice d(sslStream, fUseSSL);
        iostreams::stream<SSLIOStreamDevice> stream(d);
#else
        ip::tcp::iostream stream;
#endif

        ip::tcp::endpoint peer;
        vnThreadsRunning[THREAD_RPCSERVER]--;
#ifdef USE_SSL
        acceptor.accept(sslStream.lowest_layer(), peer);
#else
        acceptor.accept(*stream.rdbuf(), peer);
#endif
        vnThreadsRunning[4]++;
        if (fShutdown)
            return;

        // Restrict callers by IP
        if (!ClientAllowed(peer.address().to_string()))
        {
            // Only send a 403 if we're not using SSL to prevent a DoS during the SSL handshake.
            if (!fUseSSL)
                stream << HTTPReply(403, "") << std::flush;
            continue;
        }

        map<string, string> mapHeaders;
        string strRequest;

        boost::thread api_caller(ReadHTTP, boost::ref(stream), boost::ref(mapHeaders), boost::ref(strRequest));
        if (!api_caller.timed_join(boost::posix_time::seconds(GetArg("-rpctimeout", 30))))
        {   // Timed out:
            acceptor.cancel();
            printf("ThreadRPCServer ReadHTTP timeout\n");
            continue;
        }

        // Check authorization
        if (mapHeaders.count("authorization") == 0)
        {
            stream << HTTPReply(401, "") << std::flush;
            continue;
        }
        if (!HTTPAuthorized(mapHeaders))
        {
            printf("ThreadRPCServer incorrect password attempt from %s\n",peer.address().to_string().c_str());
            /* Deter brute-forcing short passwords.
               If this results in a DOS the user really
               shouldn't have their RPC port exposed.*/
            if (mapArgs["-rpcpassword"].size() < 20)
                Sleep(250);

            stream << HTTPReply(401, "") << std::flush;
            continue;
        }

        Value id = Value::null;
        try
        {
            // Parse request
            Value valRequest;
            if (!read_string(strRequest, valRequest) || valRequest.type() != obj_type)
                throw JSONRPCError(-32700, "Parse error");
            const Object& request = valRequest.get_obj();

            // Parse id now so errors from here on will have the id
            id = find_value(request, "id");

            // Parse method
            Value valMethod = find_value(request, "method");
            if (valMethod.type() == null_type)
                throw JSONRPCError(-32600, "Missing method");
            if (valMethod.type() != str_type)
                throw JSONRPCError(-32600, "Method must be a string");
            string strMethod = valMethod.get_str();
            if (strMethod != "getwork" && strMethod != "getmemorypool")
                printf("ThreadRPCServer method=%s\n", strMethod.c_str());

            // Parse params
            Value valParams = find_value(request, "params");
            Array params;
            if (valParams.type() == array_type)
                params = valParams.get_array();
            else if (valParams.type() == null_type)
                params = Array();
            else
                throw JSONRPCError(-32600, "Params must be an array");

            // Find method
            map<string, rpcfn_type>::iterator mi = mapCallTable.find(strMethod);
            if (mi == mapCallTable.end())
                throw JSONRPCError(-32601, "Method not found");

            // Observe safe mode
            string strWarning = GetWarnings("rpc");
            if (strWarning != "" && !GetBoolArg("-disablesafemode") && !setAllowInSafeMode.count(strMethod))
                throw JSONRPCError(-2, string("Safe mode: ") + strWarning);

            try
            {
                // Execute
                Value result;
                CRITICAL_BLOCK(cs_main)
                CRITICAL_BLOCK(pwalletMain->cs_wallet)
                    result = (*(*mi).second)(params, false);

                // Send reply
                string strReply = JSONRPCReply(result, Value::null, id);
                stream << HTTPReply(200, strReply) << std::flush;
            }
            catch (std::exception& e)
            {
                ErrorReply(stream, JSONRPCError(-1, e.what()), id);
            }
        }
        catch (Object& objError)
        {
            ErrorReply(stream, objError, id);
        }
        catch (std::exception& e)
        {
            ErrorReply(stream, JSONRPCError(-32700, e.what()), id);
        }
    }
}




Object CallRPC(const string& strMethod, const Array& params)
{
    if (mapArgs["-rpcuser"] == "" && mapArgs["-rpcpassword"] == "")
        throw runtime_error(strprintf(
            _("You must set rpcpassword=<password> in the configuration file:\n%s\n"
              "If the file does not exist, create it with owner-readable-only file permissions."),
                GetConfigFile().c_str()));

    // Connect to localhost
    bool fUseSSL = GetBoolArg("-rpcssl");
#ifdef USE_SSL
    asio::io_service io_service;
    ssl::context context(io_service, ssl::context::sslv23);
    context.set_options(ssl::context::no_sslv2);
    SSLStream sslStream(io_service, context);
    SSLIOStreamDevice d(sslStream, fUseSSL);
    iostreams::stream<SSLIOStreamDevice> stream(d);
    if (!d.connect(GetArg("-rpcconnect", "127.0.0.1"), GetArg("-rpcport", "9332")))
        throw runtime_error("couldn't connect to server");
#else
    if (fUseSSL)
        throw runtime_error("-rpcssl=1, but litecoin compiled without full openssl libraries.");

    ip::tcp::iostream stream(GetArg("-rpcconnect", "127.0.0.1"), GetArg("-rpcport", "9332"));
    if (stream.fail())
        throw runtime_error("couldn't connect to server");
#endif


    // HTTP basic authentication
    string strUserPass64 = EncodeBase64(mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"]);
    map<string, string> mapRequestHeaders;
    mapRequestHeaders["Authorization"] = string("Basic ") + strUserPass64;

    // Send request
    string strRequest = JSONRPCRequest(strMethod, params, 1);
    string strPost = HTTPPost(strRequest, mapRequestHeaders);
    stream << strPost << std::flush;

    // Receive reply
    map<string, string> mapHeaders;
    string strReply;
    int nStatus = ReadHTTP(stream, mapHeaders, strReply);
    if (nStatus == 401)
        throw runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
    else if (nStatus >= 400 && nStatus != 400 && nStatus != 404 && nStatus != 500)
        throw runtime_error(strprintf("server returned HTTP error %d", nStatus));
    else if (strReply.empty())
        throw runtime_error("no response from server");

    // Parse reply
    Value valReply;
    if (!read_string(strReply, valReply))
        throw runtime_error("couldn't parse reply from server");
    const Object& reply = valReply.get_obj();
    if (reply.empty())
        throw runtime_error("expected reply to have result, error and id properties");

    return reply;
}




template<typename T>
void ConvertTo(Value& value)
{
    if (value.type() == str_type)
    {
        // reinterpret string as unquoted json value
        Value value2;
        if (!read_string(value.get_str(), value2))
            throw runtime_error("type mismatch");
        value = value2.get_value<T>();
    }
    else
    {
        value = value.get_value<T>();
    }
}

int CommandLineRPC(int argc, char *argv[])
{
    string strPrint;
    int nRet = 0;
    try
    {
        // Skip switches
        while (argc > 1 && IsSwitchChar(argv[1][0]))
        {
            argc--;
            argv++;
        }

        // Method
        if (argc < 2)
            throw runtime_error("too few parameters");
        string strMethod = argv[1];

        // Parameters default to strings
        Array params;
        for (int i = 2; i < argc; i++)
            params.push_back(argv[i]);
        int n = params.size();

        //
        // Special case non-string parameter types
        //
        if (strMethod == "setgenerate"            && n > 0) ConvertTo<bool>(params[0]);
        if (strMethod == "setgenerate"            && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "getnetworkhashps"       && n > 0) ConvertTo<boost::int64_t>(params[0]);
        if (strMethod == "sendtoaddress"          && n > 1) ConvertTo<double>(params[1]);
        if (strMethod == "settxfee"               && n > 0) ConvertTo<double>(params[0]);
        if (strMethod == "setmininput"            && n > 0) ConvertTo<double>(params[0]);
        if (strMethod == "getreceivedbyaddress"   && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "getreceivedbyaccount"   && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "listreceivedbyaddress"  && n > 0) ConvertTo<boost::int64_t>(params[0]);
        if (strMethod == "listreceivedbyaddress"  && n > 1) ConvertTo<bool>(params[1]);
        if (strMethod == "listreceivedbyaccount"  && n > 0) ConvertTo<boost::int64_t>(params[0]);
        if (strMethod == "listreceivedbyaccount"  && n > 1) ConvertTo<bool>(params[1]);
        if (strMethod == "getbalance"             && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "getblockhash"           && n > 0) ConvertTo<boost::int64_t>(params[0]);
        if (strMethod == "move"                   && n > 2) ConvertTo<double>(params[2]);
        if (strMethod == "move"                   && n > 3) ConvertTo<boost::int64_t>(params[3]);
        if (strMethod == "sendfrom"               && n > 2) ConvertTo<double>(params[2]);
        if (strMethod == "sendfrom"               && n > 3) ConvertTo<boost::int64_t>(params[3]);
        if (strMethod == "listtransactions"       && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "listtransactions"       && n > 2) ConvertTo<boost::int64_t>(params[2]);
        if (strMethod == "listaccounts"           && n > 0) ConvertTo<boost::int64_t>(params[0]);
        if (strMethod == "walletpassphrase"       && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "listsinceblock"         && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "sendmany"               && n > 1)
        {
            string s = params[1].get_str();
            Value v;
            if (!read_string(s, v) || v.type() != obj_type)
                throw runtime_error("type mismatch");
            params[1] = v.get_obj();
        }
        if (strMethod == "sendmany"                && n > 2) ConvertTo<boost::int64_t>(params[2]);
        if (strMethod == "addmultisigaddress"      && n > 0) ConvertTo<boost::int64_t>(params[0]);
        if (strMethod == "addmultisigaddress"      && n > 1)
        {
            string s = params[1].get_str();
            Value v;
            if (!read_string(s, v) || v.type() != array_type)
                throw runtime_error("type mismatch "+s);
            params[1] = v.get_array();
        }

        // Execute
        Object reply = CallRPC(strMethod, params);

        // Parse reply
        const Value& result = find_value(reply, "result");
        const Value& error  = find_value(reply, "error");

        if (error.type() != null_type)
        {
            // Error
            strPrint = "error: " + write_string(error, false);
            int code = find_value(error.get_obj(), "code").get_int();
            nRet = abs(code);
        }
        else
        {
            // Result
            if (result.type() == null_type)
                strPrint = "";
            else if (result.type() == str_type)
                strPrint = result.get_str();
            else
                strPrint = write_string(result, true);
        }
    }
    catch (std::exception& e)
    {
        strPrint = string("error: ") + e.what();
        nRet = 87;
    }
    catch (...)
    {
        PrintException(NULL, "CommandLineRPC()");
    }

    if (strPrint != "")
    {
        fprintf((nRet == 0 ? stdout : stderr), "%s\n", strPrint.c_str());
    }
    return nRet;
}




#ifdef TEST
int main(int argc, char *argv[])
{
#ifdef _MSC_VER
    // Turn off microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFile("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
    setbuf(stdin, NULL);
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    try
    {
        if (argc >= 2 && string(argv[1]) == "-server")
        {
            printf("server ready\n");
            ThreadRPCServer(NULL);
        }
        else
        {
            return CommandLineRPC(argc, argv);
        }
    }
    catch (std::exception& e) {
        PrintException(&e, "main()");
    } catch (...) {
        PrintException(NULL, "main()");
    }
    return 0;
}
#endif
