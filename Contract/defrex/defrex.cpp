#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/asset.hpp>
#include <vector>

using namespace eosio;
using namespace std;

class[[eosio::contract]] defrex : public eosio::contract
{

  public:
	using contract::contract;

	defrex(name receiver, name code, datastream<const char *> ds) : contract(receiver, code, ds){};

	//액션
	[[eosio::action]] void create(name user, asset A, name codeA, asset B, name codeB, short fee, short reserveratio) {
		require_auth(user);
		eosio_assert(A.amount >= B.amount, "MUST A>=B");
		eosio_assert(fee + reserveratio < 10000 && fee >= 0 && reserveratio >= 0, "(fee+reserve ratio)<10000");
		trades_table trades(_code, _code.value);
		trades.emplace(_code, [&](auto &r) {
			r.idx = trades.available_primary_key();
			r.manager.name = user;
			r.A.fee.symbol = A.symbol;
			r.A.reserve.symbol = A.symbol;
			r.A.code = codeA;
			r.B.fee.symbol = B.symbol;
			r.B.reserve.symbol = B.symbol;
			r.B.code = codeB;
			r.ratio = A.amount / B.amount;
			r.fee = fee;
			r.reserveratio = reserveratio;
		});
		sendSummary(user, std::to_string(trades.available_primary_key() - 1));
	};

	[[eosio::action]] void claim(uint64_t idx) 
	{
		trades_table trades(_code, _code.value);
		auto itr_trade = trades.find(idx);
		eosio_assert(itr_trade != trades.end(), std::string("Trade not exists").c_str());
		if (itr_trade->A.fee.amount > 0)
			action(
				permission_level{_code, "active"_n},
				itr_trade->A.code,
				"transfer"_n,
				std::make_tuple(_code, itr_trade->manager.name, itr_trade->A.fee, std::string("here you are :D")))
				.send();

		if (itr_trade->B.fee.amount > 0)
			action(
				permission_level{_code, "active"_n},
				itr_trade->B.code,
				"transfer"_n,
				std::make_tuple(_code, itr_trade->manager.name, itr_trade->B.fee, std::string("here you are :D")))
				.send();

		trades.modify(itr_trade, _code, [&](auto &r) {
			r.A.fee.amount = 0;
			r.B.fee.amount = 0;
		});
	}

	[[eosio::action]] void income() {
		auto transfer_data = unpack_action_data<st_transfer>();
		bool fromIsKey = false;

		public_key fromKey;
		string fromKeyStr;
		vector<string> idxData;
		vector<string> extraData;

		string memo_data = transfer_data.memo;
		name sa;

		if (transfer_data.from == _self || transfer_data.to != _self)
			return;
		idxData = split(memo_data.c_str(), '|');
		extraData = split(idxData[1].c_str(), '$');
		if (transfer_data.from.value == name("").value)
		{
			fromIsKey = true;
			switch (extraData.size())
			{
			case 4:
				fromKey = str_to_pub(extraData[1]);
				fromKeyStr = extraData[1];
				break;
			case 5:
				fromKey = str_to_pub(extraData[2]);
				fromKeyStr = extraData[2];
				break; // from은 무조건 name인데 그럼 이 부분은 필요없는거 아닌가?
			}
		}

		/*if (transfer_data.memo.find('|') == string::npos)
			return;*/
		eosio_assert(transfer_data.memo.find('|') != string::npos, std::string("token Format not right").c_str());
		switch (name{extraData[0]}.value)
		{
		case "trade"_n.value:
			fromIsKey ? trade(fromKeyStr, transfer_data.to, transfer_data.quantity, idxData[0]) : trade(transfer_data.from, transfer_data.to, transfer_data.quantity, idxData[0]);
			break;
		case "topupreserve"_n.value:
			topupreserve(transfer_data.to, transfer_data.quantity, idxData[0]);
			break;
		}
	}

	[[eosio::action]] void notify(name user, std::string msg)
	{
		require_auth(user);
		require_recipient(user);
	}

  private:
	//함수
	void topupreserve(name to, asset amount, std::string memo)
	{
		uint64_t idx = (uint64_t)std::stoi(memo);
		trades_table trades(to, to.value);
		auto itr_trade = trades.find(idx);
		eosio_assert(itr_trade != trades.end(), std::string("Trade [" + memo + "] not exists").c_str());
		trades.modify(itr_trade, to, [&](auto &r) {
			eosio_assert((amount.symbol == r.A.reserve.symbol && _code.value == r.A.code.value) || (amount.symbol == r.B.reserve.symbol && _code.value == r.B.code.value), "WRONG TOKEN!!");
			if (amount.symbol == r.A.reserve.symbol && _code.value == r.A.code.value)
				r.A.reserve.amount += amount.amount;
			else if (amount.symbol == r.B.reserve.symbol && _code.value == r.B.code.value)
				r.B.reserve.amount += amount.amount;
			
		});
	}

	void trade(name from, name to, asset amount, std::string memo)
	{
		uint64_t idx = (uint64_t)std::stoi(memo);

		trades_table trades(to, to.value);
		auto itr_trade = trades.find(idx);
		eosio_assert(itr_trade != trades.end(), "Trades not exists");
		trades.modify(itr_trade, to, [&](auto &r) {
			uint64_t fee = (amount.amount * r.fee / 10000);
			uint64_t reservefee = (amount.amount * r.reserveratio / 10000);
			uint64_t value = amount.amount - fee - reservefee;
			asset send;
			if (_code.value == r.A.code.value)
				send.amount = value / r.ratio;
			else if (_code.value == r.B.code.value)
				send.amount = value * r.ratio;
			eosio_assert((amount.symbol == r.A.reserve.symbol && _code.value == r.A.code.value) || (amount.symbol == r.B.reserve.symbol && _code.value == r.B.code.value), "WRONG TOKEN!!");
			if (amount.symbol == r.A.reserve.symbol && _code.value == r.A.code.value)
			{
				eosio_assert(r.B.reserve.amount >= send.amount, "Not enough reserve, try again later");
				r.A.reserve.amount += value + reservefee;
				r.A.fee.amount += fee;
				r.B.reserve.amount -= send.amount;
				send.symbol = r.B.reserve.symbol;
				action(
					permission_level{to, "active"_n},
					r.B.code,
					"transfer"_n,
					std::make_tuple(to, from, send, std::string("here you are :D")))
					.send();
			}
			else if (amount.symbol == r.B.reserve.symbol && _code.value == r.B.code.value)
			{
				eosio_assert(r.A.reserve.amount >= send.amount, "Not enough reserve, try again later");
				r.B.reserve.amount += value + reservefee;
				r.B.fee.amount += fee;
				r.A.reserve.amount -= send.amount;
				send.symbol = r.A.reserve.symbol;
				action(
					permission_level{to, "active"_n},
					r.A.code,
					"transfer"_n,
					std::make_tuple(to, from, send, std::string("here you are :D")))
					.send();
			}
		});
	}
	void trade(string from, name to, asset amount, std::string memo)
	{
		uint64_t idx = (uint64_t)std::stoi(memo);

		trades_table trades(to, to.value);
		auto itr_trade = trades.find(idx);
		eosio_assert(itr_trade != trades.end(), "Trades not exists");
		trades.modify(itr_trade, to, [&](auto &r) {
			uint64_t fee = (amount.amount * r.fee / 10000);
			uint64_t reservefee = (amount.amount * r.reserveratio / 10000);
			uint64_t value = amount.amount - fee - reservefee;
			asset send;
			send.amount = value / r.ratio;
			eosio_assert((amount.symbol == r.A.reserve.symbol && _code.value == r.A.code.value) || (amount.symbol == r.B.reserve.symbol && _code.value == r.B.code.value), "WRONG TOKEN!!");
			if (amount.symbol == r.A.reserve.symbol && _code.value == r.A.code.value)
			{
				eosio_assert(r.B.reserve.amount >= send.amount, "Not enough reserve, try again later");
				r.A.reserve.amount += value + reservefee;
				r.A.fee.amount += fee;
				r.B.reserve.amount -= send.amount;
				send.symbol = r.B.reserve.symbol;
				action(
					permission_level{to, "active"_n},
					r.B.code,
					"transfer"_n,
					std::make_tuple(to, name(""), send, std::string("here you are :D$" + from)))
					.send();
			}
			else if (amount.symbol == r.B.reserve.symbol && _code.value == r.B.code.value)
			{
				eosio_assert(r.A.reserve.amount >= send.amount, "Not enough reserve, try again later");
				r.B.reserve.amount += value + reservefee;
				r.B.fee.amount += fee;
				r.A.reserve.amount -= send.amount;
				send.symbol = r.A.reserve.symbol;
				action(
					permission_level{to, "active"_n},
					r.A.code,
					"transfer"_n,
					std::make_tuple(to, name(""), send, std::string("here you are :D$" + from)))
					.send();
			}
		});
	}
	const char *pszBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
	const int8_t mapBase58[256] = {
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		0,
		1,
		2,
		3,
		4,
		5,
		6,
		7,
		8,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		9,
		10,
		11,
		12,
		13,
		14,
		15,
		16,
		-1,
		17,
		18,
		19,
		20,
		21,
		-1,
		22,
		23,
		24,
		25,
		26,
		27,
		28,
		29,
		30,
		31,
		32,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		33,
		34,
		35,
		36,
		37,
		38,
		39,
		40,
		41,
		42,
		43,
		-1,
		44,
		45,
		46,
		47,
		48,
		49,
		50,
		51,
		52,
		53,
		54,
		55,
		56,
		57,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
		-1,
	};
	const char ALPHABET_MAP[128] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8, -1, -1, -1, -1, -1, -1,
    -1,  9, 10, 11, 12, 13, 14, 15, 16, -1, 17, 18, 19, 20, 21, -1,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, -1, -1, -1, -1, -1,
    -1, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, -1, 44, 45, 46,
    47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, -1, -1, -1, -1, -1
	};

	int EncodeBase58(const string input, int len, unsigned char result[]) {
    	unsigned char const* bytes = (unsigned const char*)(input.c_str()); 
    	unsigned char digits[len * 137 / 100];
   		int digitslen = 1;

    	for (int i = 0; i < len; i++) {
        	unsigned int carry = (unsigned int) bytes[i];
        	for (int j = 0; j < digitslen; j++) {
            	carry += (unsigned int) (digits[j]) << 8;
            	digits[j] = (unsigned char) (carry % 58);
            	carry /= 58;
        	}
        	while (carry > 0) {
            	digits[digitslen++] = (unsigned char) (carry % 58);
            	carry /= 58;
        	}
    	}

    	int resultlen = 0;
    	// leading zero bytes
    	for (; resultlen < len && bytes[resultlen] == 0;)
        	result[resultlen++] = '1';
    	// reverse
    	for (int i = 0; i < digitslen; i++)
        	result[resultlen + i] = pszBase58[digits[digitslen - 1 - i]];
    	result[digitslen + resultlen] = 0;
    	return digitslen + resultlen;
	} //ex) EOS8KgkQikWK84J2jJ1Nvd3ttfJNRicYZsdbunbe9biR99dHGb24a -> EncodeBase58("6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV", 1 + 20 + 4, encoded);

	bool DecodeBase58(const char *psz, std::vector<unsigned char> &vch)
	{
		// Skip leading spaces.
		while (*psz && isspace(*psz))
			psz++;
		// Skip and count leading '1's.
		int zeroes = 0;
		int length = 0;
		while (*psz == '1')
		{
			zeroes++;
			psz++;
		}
		// Allocate enough space in big-endian base256 representation.
		int size = strlen(psz) * 733 / 1000 + 1; // log(58) / log(256), rounded up.
		std::vector<unsigned char> b256(size);
		// Process the characters.
		static_assert(
			sizeof(mapBase58) / sizeof(mapBase58[0]) == 256,
			"mapBase58.size() should be 256"); // guarantee not out of range
		while (*psz && !isspace(*psz))
		{
			// Decode base58 character
			int carry = mapBase58[(uint8_t)*psz];
			if (carry == -1) // Invalid b58 character
				return false;
			int i = 0;
			for (std::vector<unsigned char>::reverse_iterator it = b256.rbegin();
				 (carry != 0 || i < length) && (it != b256.rend());
				 ++it, ++i)
			{
				carry += 58 * (*it);
				*it = carry % 256;
				carry /= 256;
			}
			assert(carry == 0);
			length = i;
			psz++;
		}
		// Skip trailing spaces.
		while (isspace(*psz))
			psz++;
		if (*psz != 0)
			return false;
		// Skip leading zeroes in b256.
		std::vector<unsigned char>::iterator it = b256.begin() + (size - length);
		while (it != b256.end() && *it == 0)
			it++;
		// Copy result into output vector.
		vch.reserve(zeroes + (b256.end() - it));
		vch.assign(zeroes, 0x00);
		while (it != b256.end())
			vch.push_back(*(it++));
		return true;
	}

	bool decode_base58(const string &str, vector<unsigned char> &vch)
	{
		return DecodeBase58(str.c_str(), vch);
	}

	public_key str_to_pub(const string &pubkey, const bool &checksumming = true)
	{
		string pubkey_prefix("EOS");
		auto base58substr = pubkey.substr(pubkey_prefix.length());
		vector<unsigned char> vch;
		eosio_assert(decode_base58(base58substr, vch), "Decode public key failed");
		eosio_assert(vch.size() == 37, "Invalid public key");
		if (checksumming)
		{

			array<unsigned char, 33> pubkey_data;
			copy_n(vch.begin(), 33, pubkey_data.begin());

			capi_checksum160 check_pubkey;
			ripemd160(reinterpret_cast<char *>(pubkey_data.data()), 33, &check_pubkey);

			eosio_assert(memcmp(&check_pubkey, &vch.end()[-4], 4) == 0, "Public key checksum mismatch");
		}
		public_key _pub_key;
		unsigned int type = 0;
		_pub_key.data[0] = (char)type;
		for (int i = 1; i < sizeof(_pub_key.data); i++)
		{
			_pub_key.data[i] = vch[i - 1];
		}
		return _pub_key;
	}

	vector<string> split(const char *str, char c = ' ')
	{
		vector<string> result;

		do
		{
			const char *begin = str;

			while (*str != c && *str)
				str++;

			result.push_back(string(begin, str));
		} while (0 != *str++);

		return result;
	}

	void sendSummary(name user, string str)
	{
		action(
			permission_level{user, "active"_n},
			get_self(),
			"notify"_n,
			std::make_tuple(user, str))
			.send();
	}
	//구조체
	struct st_transfer
	{
		name from;
		name to;
		asset quantity;
		string memo;
	};
	struct Token
	{
		name code;
		asset reserve;
		asset fee;
	};
	struct account
	{
		public_key key;
		name name;
	};

	//테이블
	struct [[eosio::table]] trades
	{
		uint64_t idx;
		account manager;
		Token A;
		Token B;
		int64_t ratio;
		short fee;
		short reserveratio;

		uint64_t primary_key() const { return idx; }
	};
	typedef multi_index<"trades"_n, trades> trades_table;
};
#define EOSIO_DISPATCH_EX(TYPE, MEMBERS)                                                                        \
	extern "C"                                                                                                  \
	{                                                                                                           \
		void apply(uint64_t receiver, uint64_t code, uint64_t action)                                           \
		{                                                                                                       \
			if (code != receiver && (action >= name{"transfer"}.value && action <= name{"transferzzzz"}.value)) \
			{                                                                                                   \
				execute_action(name(receiver), name(code), &defrex::income);                                    \
			}                                                                                                   \
			if (action != name{"income"}.value)                                                                 \
			{                                                                                                   \
				switch (action)                                                                                 \
				{                                                                                               \
					EOSIO_DISPATCH_HELPER(TYPE, MEMBERS)                                                        \
				} /* does not allow destructor of thiscontract to run: eosio_exit(0); */                        \
			}                                                                                                   \
			else                                                                                                \
				eosio_exit(0);                                                                                  \
		}                                                                                                       \
	}
EOSIO_DISPATCH_EX(defrex, (create)(claim)(income)(notify))