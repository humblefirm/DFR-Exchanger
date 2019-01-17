#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/asset.hpp>

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
		eosio_assert(fee < 5000 && fee >= 0 && reserveratio < 5000 && reserveratio >= 0, "0<=(fee&reserve ratio)<5000");
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
		//eosio::print(std::to_string(trades.available_primary_key()-1));
		sendSummary(user, std::to_string(trades.available_primary_key() - 1));
	};

	[[eosio::action]] void claim(uint64_t idx) {
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
	};

	[[eosio::action]] void income() {
		auto transfer_data = unpack_action_data<st_transfer>();
		bool fromiskey=false;//is_key(transfer_data.from);
		bool toiskey=false;//is_key(transfer_data.to);
		if (fromiskey){
			auto transfer_data = unpack_action_data<st_transferk>();
		}
		if (toiskey?name(transfer_data.to):transfer_data.to != _self)
			return;
		//if (transfer_data.from == _self || transfer_data.to != _self) return;

		//uint64_t idx = (uint64_t)std::stoi(transfer_data.memo.substr(0, transfer_data.memo.find('|')));
		if (transfer_data.memo.find('|') == string::npos)
			return;
		string idx = transfer_data.memo.substr(0, transfer_data.memo.find('|'));
		string option = transfer_data.memo.substr(transfer_data.memo.find('|') + 1, transfer_data.memo.length());
		switch (name{option}.value)
		{
		case "trade"_n.value:
			trade();
			break;
		case "topupreserve"_n.value:
			topupreserve(name(transfer_data.from), name(transfer_data.to), transfer_data.quantity, idx);
			break;
		}
	}

		[[eosio::action]] void
		notify(name user, std::string msg)
	{
		require_auth(user);
		require_recipient(user);
	};

  private:
	//함수
	bool is_key(string account)
	{
		if (account.size() == 53)
			return true;
		return false;
	}
	void topupreserve(name from, name to, asset quantity, std::string memo)
	{
		uint64_t idx = (uint64_t)std::stoi(memo);
		trades_table trades(to, to.value);
		auto itr_trade = trades.find(idx);
		eosio_assert(itr_trade != trades.end(), std::string("Trade [" + memo + "] not exists").c_str());
		trades.modify(itr_trade, to, [&](auto &r) {
			eosio_assert((quantity.symbol == r.A.reserve.symbol && _code.value == r.A.code.value) || (quantity.symbol == r.B.reserve.symbol && _code.value == r.B.code.value), "WRONG TOKEN!!");
			if (quantity.symbol == r.A.reserve.symbol && _code.value == r.A.code.value)
				r.A.reserve.amount += quantity.amount;
			else if (quantity.symbol == r.B.reserve.symbol && _code.value == r.B.code.value)
				r.B.reserve.amount += quantity.amount;
		});
	};
	void trade(name from, name to, asset quantity, std::string memo)
	{
		uint64_t idx = (uint64_t)std::stoi(memo);

		trades_table trades(to, to.value);
		auto itr_trade = trades.find(idx);
		eosio_assert(itr_trade != trades.end(), "Trades not exists");
		trades.modify(itr_trade, to, [&](auto &r) {
			uint64_t fee = (quantity.amount * r.fee / 10000);
			uint64_t reservefee = (quantity.amount * r.reserveratio / 10000);
			uint64_t value = quantity.amount - fee - reservefee;
			asset send;
			eosio_assert((quantity.symbol == r.A.reserve.symbol && _code.value == r.A.code.value) || (quantity.symbol == r.B.reserve.symbol && _code.value == r.B.code.value), "WRONG TOKEN!!");
			if (quantity.symbol == r.A.reserve.symbol && _code.value == r.A.code.value)
			{
				send.amount = value / r.ratio;
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
			else if (quantity.symbol == r.B.reserve.symbol && _code.value == r.B.code.value)
			{
				send.amount = value * r.ratio;
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
	};
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
		uint64_t get_secondary_A() const { return A.code.value; }
		uint64_t get_secondary_B() const { return B.code.value; }
	};
	typedef multi_index<"trades"_n, trades,
						indexed_by<"bya"_n, const_mem_fun<trades, uint64_t, &trades::get_secondary_A>>,
						indexed_by<"byb"_n, const_mem_fun<trades, uint64_t, &trades::get_secondary_B>>>
		trades_table;
};
#define EOSIO_DISPATCH_EX(TYPE, MEMBERS)                                                 \
	extern "C"                                                                           \
	{                                                                                    \
		void apply(uint64_t receiver, uint64_t code, uint64_t action)                    \
		{                                                                                \
			if (code != receiver && action == name{"transfer"}.value)                    \
			{                                                                            \
				execute_action(name(receiver), name(code), &defrex::income);             \
			}                                                                            \
			if (action != name{"income"}.value)                                          \
			{                                                                            \
				switch (action)                                                          \
				{                                                                        \
					EOSIO_DISPATCH_HELPER(TYPE, MEMBERS)                                 \
				} /* does not allow destructor of thiscontract to run: eosio_exit(0); */ \
			}                                                                            \
			else                                                                         \
				eosio_exit(0);                                                           \
		}                                                                                \
	}
EOSIO_DISPATCH_EX(defrex, (create)(claim)(income)(notify))