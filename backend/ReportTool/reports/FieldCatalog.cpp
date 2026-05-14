//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       FieldCatalog.cpp           |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "FieldCatalog.h"
#include "Classifier.h"
#include "Expression.h"
#include <stdexcept>
#include <unordered_map>
#include <set>

using nlohmann::json;
using namespace FieldCatalog;

const char* FieldCatalog::SourceName(Source s)
{
   switch(s)
   {
      case Source::User:      return "user";
      case Source::Account:   return "account";
      case Source::Daily:     return "daily";
      case Source::Deal:      return "deal";
      case Source::Position:  return "position";
      case Source::OrderOpen: return "order_open";
      case Source::OrderHist: return "order_hist";
      case Source::Literal:   return "literal";
   }
   return "?";
}

bool FieldCatalog::SourceFromName(const std::string& s, Source* out)
{
   if(s == "user")       { *out = Source::User; return true; }
   if(s == "account")    { *out = Source::Account; return true; }
   if(s == "daily")      { *out = Source::Daily; return true; }
   if(s == "deal")       { *out = Source::Deal; return true; }
   if(s == "position")   { *out = Source::Position; return true; }
   if(s == "order_open") { *out = Source::OrderOpen; return true; }
   if(s == "order_hist") { *out = Source::OrderHist; return true; }
   return false;
}

namespace
{
   //--- Throw-on-missing source helper.
   template <class T>
   inline const T& Need(const T* p, const char* what)
   {
      if(!p) throw std::runtime_error(std::string("data source not loaded: ") + what);
      return *p;
   }

   //--- Thread-local regex compile cache (keyed by pattern).
   const std::regex& CachedRegex(const std::string& pattern)
   {
      static thread_local std::unordered_map<std::string, std::regex> cache;
      auto it = cache.find(pattern);
      if(it != cache.end()) return it->second;
      auto ins = cache.emplace(pattern,
                                std::regex(pattern, std::regex::ECMAScript | std::regex::icase));
      return ins.first->second;
   }

   //--- Lowercase ASCII helper for case-insensitive contains/starts/ends.
   std::string LowerAscii(const std::string& s)
   {
      std::string out; out.reserve(s.size());
      for(char c : s) out += (char)std::tolower((unsigned char)c);
      return out;
   }

   bool CmpNumeric(double lhs, FilterOp op, double rhs)
   {
      switch(op)
      {
         case FilterOp::Eq:  return lhs == rhs;
         case FilterOp::Neq: return lhs != rhs;
         case FilterOp::Lt:  return lhs <  rhs;
         case FilterOp::Lte: return lhs <= rhs;
         case FilterOp::Gt:  return lhs >  rhs;
         case FilterOp::Gte: return lhs >= rhs;
         default: throw std::runtime_error("numeric field doesn't support op '"
                                           + std::string(FilterOpName(op)) + "'");
      }
   }

   bool CmpText(const std::string& lhs, FilterOp op, const std::string& rhs)
   {
      if(op == FilterOp::Eq)  return lhs == rhs;
      if(op == FilterOp::Neq) return lhs != rhs;
      if(op == FilterOp::Regex)
         return std::regex_search(lhs, CachedRegex(rhs));
      if(op == FilterOp::Contains)
         return LowerAscii(lhs).find(LowerAscii(rhs)) != std::string::npos;
      if(op == FilterOp::StartsWith)
      {
         auto L = LowerAscii(lhs), R = LowerAscii(rhs);
         return L.size() >= R.size() && L.compare(0, R.size(), R) == 0;
      }
      if(op == FilterOp::EndsWith)
      {
         auto L = LowerAscii(lhs), R = LowerAscii(rhs);
         return L.size() >= R.size() && L.compare(L.size() - R.size(), R.size(), R) == 0;
      }
      throw std::runtime_error("text field doesn't support op '" + std::string(FilterOpName(op)) + "'");
   }

   //--- Generic Cmp eval given accessor function pointers per source. Returns true
   //--- if predicate leaf matches the row. `get_num` returns NaN when field is text-typed
   //--- and vice versa; the cmp.is_numeric flag selects path.
   template <class RowT>
   bool EvalCmpRow(const FieldFilter& f, const RowT& row,
                   double (*get_num)(const RowT&, const std::string&),
                   std::string (*get_text)(const RowT&, const std::string&))
   {
      if(f.op == FilterOp::In)
      {
         if(f.is_numeric)
         {
            const double v = get_num(row, f.field);
            for(double x : f.value_list_num) if(v == x) return true;
            return false;
         }
         const std::string v = get_text(row, f.field);
         for(const auto& x : f.value_list) if(v == x) return true;
         return false;
      }
      if(f.is_numeric)
         return CmpNumeric(get_num(row, f.field), f.op, f.value_num);
      //--- text field
      return CmpText(get_text(row, f.field), f.op, f.value_str);
   }

   //--- Predicate tree walker, parameterized over row accessors.
   template <class RowT>
   bool EvalPredicateRow(const Predicate& p, const RowT& row,
                         double (*get_num)(const RowT&, const std::string&),
                         std::string (*get_text)(const RowT&, const std::string&))
   {
      switch(p.kind)
      {
         case Predicate::Kind::Cmp:
            return EvalCmpRow(p.cmp, row, get_num, get_text);
         case Predicate::Kind::And:
            for(const auto& c : p.children) if(c && !EvalPredicateRow(*c, row, get_num, get_text)) return false;
            return true;
         case Predicate::Kind::Or:
            for(const auto& c : p.children) if(c && EvalPredicateRow(*c, row, get_num, get_text)) return true;
            return false;
         case Predicate::Kind::Not:
            return !(p.child && EvalPredicateRow(*p.child, row, get_num, get_text));
      }
      return true;
   }

   //--- Source-specific accessors -----------------------------------

   double DealGetNum(const DealRow& d, const std::string& f)
   {
      if(f == "profit")        return d.profit;
      if(f == "profit_raw")    return d.profit_raw;
      if(f == "storage")       return d.storage;
      if(f == "commission")    return d.commission;
      if(f == "fee")           return d.fee;
      if(f == "value")         return d.value;
      if(f == "volume")        return (double)d.volume;
      if(f == "volume_ext")    return (double)d.volume_ext;
      if(f == "volume_closed") return (double)d.volume_closed;
      if(f == "price")         return d.price;
      if(f == "time")          return (double)d.time;
      if(f == "action")        return (double)d.action;
      if(f == "entry")         return (double)d.entry;
      if(f == "reason")        return (double)d.reason;
      if(f == "trade_lifetime") return (double)d.trade_lifetime_sec;
      throw std::runtime_error("deal: no numeric field '" + f + "'");
   }
   std::string DealGetText(const DealRow& d, const std::string& f)
   {
      if(f == "comment")     return d.comment;
      if(f == "symbol")      return d.symbol;
      if(f == "gateway")     return d.gateway;
      if(f == "external_id") return d.external_id;
      throw std::runtime_error("deal: no text field '" + f + "'");
   }

   double DailyGetNum(const DailyRow& r, const std::string& f)
   {
      if(f == "balance")           return r.balance;
      if(f == "credit")            return r.credit;
      if(f == "profit_equity")     return r.profit_equity;
      if(f == "profit")            return r.profit;
      if(f == "margin")            return r.margin;
      if(f == "margin_free")       return r.margin_free;
      if(f == "margin_level")      return r.margin_level;
      if(f == "daily_balance")     return r.daily_balance;
      if(f == "daily_credit")      return r.daily_credit;
      if(f == "daily_profit")      return r.daily_profit;
      if(f == "daily_charge")      return r.daily_charge;
      if(f == "daily_correction")  return r.daily_correction;
      if(f == "daily_bonus")       return r.daily_bonus;
      if(f == "daily_storage")     return r.daily_storage;
      if(f == "daily_agent")       return r.daily_agent;
      if(f == "daily_interest")    return r.daily_interest;
      if(f == "datetime")          return (double)r.datetime;
      throw std::runtime_error("daily: no numeric field '" + f + "'");
   }
   std::string DailyGetText(const DailyRow& r, const std::string& f)
   {
      throw std::runtime_error("daily: no text field '" + f + "'");
   }

   double PositionGetNum(const PositionRow& p, const std::string& f)
   {
      if(f == "action")      return (double)p.action;
      if(f == "volume")      return (double)p.volume;
      if(f == "profit")      return p.profit;
      if(f == "storage")     return p.storage;
      if(f == "price_open")  return p.price_open;
      if(f == "time_create") return (double)p.time_create;
      throw std::runtime_error("position: no numeric field '" + f + "'");
   }
   std::string PositionGetText(const PositionRow& p, const std::string& f)
   {
      if(f == "symbol")  return p.symbol;
      if(f == "comment") return p.comment;
      throw std::runtime_error("position: no text field '" + f + "'");
   }

   double UserGetNum(const UserInfo& u, const std::string& f)
   {
      if(f == "agent")              return (double)u.agent;
      if(f == "client_id")          return (double)u.client_id;
      if(f == "leverage")           return (double)u.leverage;
      if(f == "language")           return (double)u.language;
      if(f == "rights")             return (double)u.rights;
      if(f == "balance")            return u.balance;
      if(f == "credit")             return u.credit;
      if(f == "interest_rate")      return u.interest_rate;
      if(f == "balance_prev_day")   return u.balance_prev_day;
      if(f == "balance_prev_month") return u.balance_prev_month;
      if(f == "equity_prev_day")    return u.equity_prev_day;
      if(f == "equity_prev_month")  return u.equity_prev_month;
      if(f == "registration")       return (double)u.registration;
      if(f == "last_access")        return (double)u.last_access;
      if(f == "last_pass_change")   return (double)u.last_pass_change;
      throw std::runtime_error("user: no numeric field '" + f + "'");
   }
   std::string UserGetText(const UserInfo& u, const std::string& f)
   {
      if(f == "name")          return u.name;
      if(f == "first_name")    return u.first_name;
      if(f == "last_name")     return u.last_name;
      if(f == "middle_name")   return u.middle_name;
      if(f == "email")         return u.email;
      if(f == "phone")         return u.phone;
      if(f == "country")       return u.country;
      if(f == "state")         return u.state;
      if(f == "city")          return u.city;
      if(f == "zip_code")      return u.zip_code;
      if(f == "address")       return u.address;
      if(f == "id")            return u.id;
      if(f == "company")       return u.company;
      if(f == "account_tag")   return u.account_tag;
      if(f == "status")        return u.status;
      if(f == "comment")       return u.comment;
      if(f == "lead_campaign") return u.lead_campaign;
      if(f == "lead_source")   return u.lead_source;
      if(f == "last_ip")       return u.last_ip;
      if(f == "group")         return u.group;
      throw std::runtime_error("user: no text field '" + f + "'");
   }

   double OrderGetNum(const OrderRow& o, const std::string& f)
   {
      if(f == "state")          return (double)o.state;
      if(f == "type")           return (double)o.type;
      if(f == "type_fill")      return (double)o.type_fill;
      if(f == "type_time")      return (double)o.type_time;
      if(f == "volume_initial") return (double)o.volume_initial;
      if(f == "volume_current") return (double)o.volume_current;
      if(f == "time_setup")     return (double)o.time_setup;
      if(f == "time_done")      return (double)o.time_done;
      if(f == "price_order")    return o.price_order;
      throw std::runtime_error("order: no numeric field '" + f + "'");
   }
   std::string OrderGetText(const OrderRow& o, const std::string& f)
   {
      if(f == "symbol")      return o.symbol;
      if(f == "comment")     return o.comment;
      if(f == "external_id") return o.external_id;
      throw std::runtime_error("order: no text field '" + f + "'");
   }
}

bool FieldCatalog::EvalDealPredicate(const Predicate& p, const DealRow& d)
{
   return EvalPredicateRow(p, d, DealGetNum, DealGetText);
}
bool FieldCatalog::EvalDailyPredicate(const Predicate& p, const DailyRow& d)
{
   return EvalPredicateRow(p, d, DailyGetNum, DailyGetText);
}
bool FieldCatalog::EvalPositionPredicate(const Predicate& p, const PositionRow& d)
{
   return EvalPredicateRow(p, d, PositionGetNum, PositionGetText);
}
bool FieldCatalog::EvalOrderPredicate(const Predicate& p, const OrderRow& d)
{
   return EvalPredicateRow(p, d, OrderGetNum, OrderGetText);
}
bool FieldCatalog::EvalUserPredicate(const Predicate& p, const UserInfo& d)
{
   return EvalPredicateRow(p, d, UserGetNum, UserGetText);
}

namespace
{
   void CollectPredicateFieldsRec(const Predicate& p,
                                  std::vector<std::string>* out,
                                  std::unordered_set<std::string>* seen)
   {
      if(p.kind == Predicate::Kind::Cmp)
      {
         if(seen->insert(p.cmp.field).second) out->push_back(p.cmp.field);
         return;
      }
      if(p.kind == Predicate::Kind::Not)
      {
         if(p.child) CollectPredicateFieldsRec(*p.child, out, seen);
         return;
      }
      for(const auto& c : p.children) if(c) CollectPredicateFieldsRec(*c, out, seen);
   }
}

std::vector<std::string> FieldCatalog::CollectPredicateFields(const Predicate& p)
{
   std::vector<std::string> out;
   std::unordered_set<std::string> seen;
   CollectPredicateFieldsRec(p, &out, &seen);
   return out;
}

std::string FieldCatalog::GetUserFieldString(const UserInfo& u, const std::string& field)
{
   try { return UserGetText(u, field); } catch(...) {}
   try
   {
      const double n = UserGetNum(u, field);
      char buf[64];
      //--- preserve integral display for whole numbers; otherwise %g
      if(n == (double)(int64_t)n)
         snprintf(buf, sizeof(buf), "%lld", (long long)n);
      else
         snprintf(buf, sizeof(buf), "%g", n);
      return buf;
   }
   catch(...) { return ""; }
}

namespace
{

   //--- Pick latest DailyRow with datetime <= target.
   const DailyRow* PickLatestAtOrBefore(const std::vector<DailyRow>& v, int64_t target)
   {
      const DailyRow* hit = nullptr;
      for(const auto& r : v)
      {
         if(r.datetime <= target) hit = &r;
         else break;
      }
      return hit;
   }

   //--- Daily snapshot start: target = D - 86400
   double DailyStart(const EvalContext& ctx, int64_t D, double (*sel)(const DailyRow&))
   {
      const auto& v = Need(ctx.daily, "daily");
      const DailyRow* r = PickLatestAtOrBefore(v, D - 86400);
      return r ? sel(*r) : 0.0;
   }

   //--- Daily snapshot end: target = D
   double DailyEnd(const EvalContext& ctx, int64_t D, double (*sel)(const DailyRow&))
   {
      const auto& v = Need(ctx.daily, "daily");
      const DailyRow* r = PickLatestAtOrBefore(v, D);
      return r ? sel(*r) : 0.0;
   }

   //--- Daily-range sum: from ≤ datetime < to (to is exclusive, like deals).
   double DailyRangeSum(const EvalContext& ctx, int64_t from, int64_t to_excl,
                        double (*sel)(const DailyRow&), const Predicate* user_pred)
   {
      const auto& v = Need(ctx.daily, "daily");
      double acc = 0.0;
      for(const auto& r : v)
      {
         if(r.datetime <  from)    continue;
         if(r.datetime >= to_excl) break;
         if(user_pred && !FieldCatalog::EvalDailyPredicate(*user_pred, r)) continue;
         acc += sel(r);
      }
      return acc;
   }

   //--- Deal-range bucket sum. Bucket numbers come from Classifier::Bucket.
   //--- `take_abs`: sum |profit|. `negate`: multiply result by -1.
   double DealBucketSum(const EvalContext& ctx, int64_t from, int64_t to_excl,
                        int bucket, bool take_abs, bool negate, const Predicate* user_pred)
   {
      const auto& deals = Need(ctx.deals, "deal");
      const auto* filters = ctx.filters;
      if(!filters) throw std::runtime_error("filters not bound for deal bucket field");
      double acc = 0.0;
      for(const auto& d : deals)
      {
         if(d.time < from || d.time >= to_excl) continue;
         if(Classifier::Bucket(d, *filters) != bucket) continue;
         if(user_pred && !FieldCatalog::EvalDealPredicate(*user_pred, d)) continue;
         acc += take_abs ? std::abs(d.profit) : d.profit;
      }
      return negate ? -acc : acc;
   }

   double DealBucketCount(const EvalContext& ctx, int64_t from, int64_t to_excl,
                          int bucket, const Predicate* user_pred)
   {
      const auto& deals = Need(ctx.deals, "deal");
      const auto* filters = ctx.filters;
      if(!filters) throw std::runtime_error("filters not bound for deal count field");
      double acc = 0.0;
      for(const auto& d : deals)
      {
         if(d.time < from || d.time >= to_excl) continue;
         if(Classifier::Bucket(d, *filters) != bucket) continue;
         if(user_pred && !FieldCatalog::EvalDealPredicate(*user_pred, d)) continue;
         acc += 1.0;
      }
      return acc;
   }

   //--- Raw per-action aggregations (no bucket gate / regex classification).
   //--- Lets the user filter freely via predicate. Covers any DEAL_* action
   //--- (balance, credit, charge, correction, bonus, commission, agent,
   //--- interest, dividend, tax, …).
   double DealActionSum(const EvalContext& ctx, int64_t from, int64_t to_excl,
                        uint32_t action, bool take_abs, const Predicate* user_pred)
   {
      const auto& deals = Need(ctx.deals, "deal");
      double acc = 0.0;
      for(const auto& d : deals)
      {
         if(d.time < from || d.time >= to_excl) continue;
         if(d.action != action) continue;
         if(user_pred && !FieldCatalog::EvalDealPredicate(*user_pred, d)) continue;
         acc += take_abs ? std::abs(d.profit) : d.profit;
      }
      return acc;
   }

   double DealActionCount(const EvalContext& ctx, int64_t from, int64_t to_excl,
                          uint32_t action, const Predicate* user_pred)
   {
      const auto& deals = Need(ctx.deals, "deal");
      double acc = 0.0;
      for(const auto& d : deals)
      {
         if(d.time < from || d.time >= to_excl) continue;
         if(d.action != action) continue;
         if(user_pred && !FieldCatalog::EvalDealPredicate(*user_pred, d)) continue;
         acc += 1.0;
      }
      return acc;
   }

   //--- Closed-trade range aggregations (action in BUY/SELL).
   //--- `sel` returns the deal-level value being aggregated.
   double TradeRangeSum(const EvalContext& ctx, int64_t from, int64_t to_excl,
                        double (*sel)(const DealRow&), const Predicate* user_pred)
   {
      const auto& deals = Need(ctx.deals, "deal");
      double acc = 0.0;
      for(const auto& d : deals)
      {
         if(d.time < from || d.time >= to_excl) continue;
         if(d.action != IMTDeal::DEAL_BUY && d.action != IMTDeal::DEAL_SELL) continue;
         if(user_pred && !FieldCatalog::EvalDealPredicate(*user_pred, d)) continue;
         acc += sel(d);
      }
      return acc;
   }

   double DealRangeCount(const EvalContext& ctx, int64_t from, int64_t to_excl,
                         bool (*pred)(const DealRow&), const Predicate* user_pred)
   {
      const auto& deals = Need(ctx.deals, "deal");
      double acc = 0.0;
      for(const auto& d : deals)
      {
         if(d.time < from || d.time >= to_excl) continue;
         if(!pred(d)) continue;
         if(user_pred && !FieldCatalog::EvalDealPredicate(*user_pred, d)) continue;
         acc += 1.0;
      }
      return acc;
   }

   //--- Closed-trade lot aggregations using volume_ext / 1e8 (symbol-independent
   //--- — MT5 SDK always reports volume_ext in 1/100000000 of a lot regardless
   //--- of the symbol's minimum volume step). Filters BUY/SELL action and a
   //--- specific entry side so opening and closing legs aren't double-counted.
   double TradeLotsSum(const EvalContext& ctx, int64_t from, int64_t to_excl,
                       bool out_only, const Predicate* user_pred)
   {
      const auto& deals = Need(ctx.deals, "deal");
      double acc = 0.0;
      for(const auto& d : deals)
      {
         if(d.time < from || d.time >= to_excl) continue;
         if(d.action != IMTDeal::DEAL_BUY && d.action != IMTDeal::DEAL_SELL) continue;
         const bool is_out = (d.entry == IMTDeal::ENTRY_OUT || d.entry == IMTDeal::ENTRY_OUT_BY);
         const bool is_in  = (d.entry == IMTDeal::ENTRY_IN  || d.entry == IMTDeal::ENTRY_INOUT);
         if(out_only) { if(!is_out) continue; }
         else         { if(!is_in)  continue; }
         if(user_pred && !FieldCatalog::EvalDealPredicate(*user_pred, d)) continue;
         acc += (double)d.volume_ext / 1e8;
      }
      return acc;
   }

   //--- Position / order aggregations.
   double PositionSum(const EvalContext& ctx, bool (*pred)(const PositionRow&),
                      double (*sel)(const PositionRow&), const Predicate* user_pred)
   {
      const auto& v = Need(ctx.positions, "position");
      double acc = 0.0;
      for(const auto& p : v)
      {
         if(pred && !pred(p)) continue;
         if(user_pred && !FieldCatalog::EvalPositionPredicate(*user_pred, p)) continue;
         acc += sel(p);
      }
      return acc;
   }

   double PositionCount(const EvalContext& ctx, bool (*pred)(const PositionRow&),
                        const Predicate* user_pred)
   {
      const auto& v = Need(ctx.positions, "position");
      double acc = 0.0;
      for(const auto& p : v)
      {
         if(pred && !pred(p)) continue;
         if(user_pred && !FieldCatalog::EvalPositionPredicate(*user_pred, p)) continue;
         acc += 1.0;
      }
      return acc;
   }

   double OpenOrderSum(const EvalContext& ctx, double (*sel)(const OpenOrderRow&),
                       const Predicate* user_pred)
   {
      const auto& v = Need(ctx.open_orders, "order_open");
      double acc = 0.0;
      for(const auto& o : v)
      {
         if(user_pred && !FieldCatalog::EvalOrderPredicate(*user_pred, o)) continue;
         acc += sel(o);
      }
      return acc;
   }

   double OpenOrderCount(const EvalContext& ctx, const Predicate* user_pred)
   {
      const auto& v = Need(ctx.open_orders, "order_open");
      if(!user_pred) return (double)v.size();
      double acc = 0.0;
      for(const auto& o : v)
         if(FieldCatalog::EvalOrderPredicate(*user_pred, o)) acc += 1.0;
      return acc;
   }

   //--- Order history aggregations. "time" for filtering = TimeDone (if non-zero) else TimeSetup.
   bool OrderInRange(const HistoryOrderRow& o, int64_t from, int64_t to_excl)
   {
      const int64_t t = o.time_done ? o.time_done : o.time_setup;
      return t >= from && t < to_excl;
   }

   double OrderHistCount(const EvalContext& ctx, int64_t from, int64_t to_excl,
                         bool (*pred)(const HistoryOrderRow&), const Predicate* user_pred)
   {
      const auto& v = Need(ctx.history_orders, "order_hist");
      double acc = 0.0;
      for(const auto& o : v)
      {
         if(!OrderInRange(o, from, to_excl)) continue;
         if(pred && !pred(o)) continue;
         if(user_pred && !FieldCatalog::EvalOrderPredicate(*user_pred, o)) continue;
         acc += 1.0;
      }
      return acc;
   }

   double OrderHistSum(const EvalContext& ctx, int64_t from, int64_t to_excl,
                       double (*sel)(const HistoryOrderRow&), const Predicate* user_pred)
   {
      const auto& v = Need(ctx.history_orders, "order_hist");
      double acc = 0.0;
      for(const auto& o : v)
      {
         if(!OrderInRange(o, from, to_excl)) continue;
         if(user_pred && !FieldCatalog::EvalOrderPredicate(*user_pred, o)) continue;
         acc += sel(o);
      }
      return acc;
   }

   //--- The mutable catalog (initialized on first All()).
   std::vector<Field>& Mutable()
   {
      static std::vector<Field> v;
      return v;
   }

   void Add(Field f) { Mutable().push_back(std::move(f)); }

   //--- Identity (text, category A). source = User but txt-only.
   void Id(const char* name, const char* label,
           std::function<std::string(const EvalContext&)> txt)
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "A"; f.category_label = "Identity";
      f.source = Source::User; f.arity = 0;
      f.return_type = "text";
      f.txt = std::move(txt);
      Add(std::move(f));
   }

   //--- Identity numeric (login). Has both num and txt.
   void IdNumeric(const char* name, const char* label,
                  std::function<double(const EvalContext&)> num,
                  std::function<std::string(const EvalContext&)> txt)
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "A"; f.category_label = "Identity";
      f.source = Source::User; f.arity = 0;
      f.return_type = "int";
      f.num = [num](const std::vector<int64_t>&, const Predicate*, const EvalContext& ctx) { return num(ctx); };
      f.txt = std::move(txt);
      Add(std::move(f));
   }

   //--- User static numeric (category B). source = User, arity 0.
   void UserNum(const char* name, const char* label, const char* return_type,
                double (*sel)(const UserInfo&))
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "B"; f.category_label = "User Static Numeric";
      f.source = Source::User; f.arity = 0;
      f.return_type = return_type;
      f.num = [sel](const std::vector<int64_t>&, const Predicate*, const EvalContext& ctx) -> double {
         const auto& u = Need(ctx.user, "user");
         return sel(u);
      };
      Add(std::move(f));
   }

   //--- Live account (category C). source = Account, arity 0.
   void Acc(const char* name, const char* label, const char* return_type,
            double (*sel)(const AccountInfo&))
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "C"; f.category_label = "Live Account Snapshot";
      f.source = Source::Account; f.arity = 0;
      f.return_type = return_type;
      f.num = [sel](const std::vector<int64_t>&, const Predicate*, const EvalContext& ctx) -> double {
         const auto& a = Need(ctx.account, "account");
         return sel(a);
      };
      Add(std::move(f));
   }

   //--- Daily snapshot start/end (category D). Registers *_start AND *_end.
   void DailyPair(const char* prefix, const char* label_prefix, const char* return_type,
                  double (*sel)(const DailyRow&))
   {
      Field s;
      s.name = std::string(prefix) + "_start";
      s.label = std::string(label_prefix) + " (start)";
      s.category = "D"; s.category_label = "Daily Snapshot Start/End";
      s.source = Source::Daily; s.arity = 1;
      s.return_type = return_type;
      s.num = [sel](const std::vector<int64_t>& d, const Predicate*, const EvalContext& ctx) -> double {
         return DailyStart(ctx, d[0], sel);
      };
      Add(std::move(s));

      Field e;
      e.name = std::string(prefix) + "_end";
      e.label = std::string(label_prefix) + " (end)";
      e.category = "D"; e.category_label = "Daily Snapshot Start/End";
      e.source = Source::Daily; e.arity = 1;
      e.return_type = return_type;
      e.num = [sel](const std::vector<int64_t>& d, const Predicate*, const EvalContext& ctx) -> double {
         return DailyEnd(ctx, d[0], sel);
      };
      Add(std::move(e));
   }

   //--- Daily Δ range sums (category E). source = Daily, arity 2.
   //--- NOTE: date_to is INCLUSIVE for the user; we convert to exclusive
   //--- midnight-of-next-day before calling the loop helper (which uses
   //--- `time < to_excl` semantics).
   void DailyRangeS(const char* name, const char* label, const char* return_type,
                    double (*sel)(const DailyRow&))
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "E"; f.category_label = "Daily Δ Range Sums";
      f.source = Source::Daily; f.arity = 2;
      f.return_type = return_type;
      f.supports_predicate = true;
      f.num = [sel](const std::vector<int64_t>& d, const Predicate* pred, const EvalContext& ctx) -> double {
         return DailyRangeSum(ctx, d[0], d[1] + 86400, sel, pred);
      };
      Add(std::move(f));
   }

   //--- Deal aggregations (category F). source = Deal, arity 2.
   void DealBucketField(const char* name, const char* label, int bucket,
                        bool take_abs, bool negate)
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "F"; f.category_label = "Deal Aggregations";
      f.source = Source::Deal; f.arity = 2;
      f.return_type = "money";
      f.supports_predicate = true;
      f.num = [bucket, take_abs, negate](const std::vector<int64_t>& d, const Predicate* pred, const EvalContext& ctx) -> double {
         return DealBucketSum(ctx, d[0], d[1] + 86400, bucket, take_abs, negate, pred);
      };
      Add(std::move(f));
   }

   void DealBucketCnt(const char* name, const char* label, int bucket)
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "F"; f.category_label = "Deal Aggregations";
      f.source = Source::Deal; f.arity = 2;
      f.return_type = "int";
      f.supports_predicate = true;
      f.num = [bucket](const std::vector<int64_t>& d, const Predicate* pred, const EvalContext& ctx) -> double {
         return DealBucketCount(ctx, d[0], d[1] + 86400, bucket, pred);
      };
      Add(std::move(f));
   }

   //--- Raw per-action sum/count fields (skip bucket gate). One action code
   //--- per field — e.g. sum_credit aggregates only DEAL_CREDIT rows.
   void DealActionField(const char* name, const char* label, uint32_t action,
                        bool take_abs)
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "F"; f.category_label = "Deal Aggregations";
      f.source = Source::Deal; f.arity = 2;
      f.return_type = "money";
      f.supports_predicate = true;
      f.num = [action, take_abs](const std::vector<int64_t>& d, const Predicate* pred, const EvalContext& ctx) -> double {
         return DealActionSum(ctx, d[0], d[1] + 86400, action, take_abs, pred);
      };
      Add(std::move(f));
   }

   void DealActionCnt(const char* name, const char* label, uint32_t action)
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "F"; f.category_label = "Deal Aggregations";
      f.source = Source::Deal; f.arity = 2;
      f.return_type = "int";
      f.supports_predicate = true;
      f.num = [action](const std::vector<int64_t>& d, const Predicate* pred, const EvalContext& ctx) -> double {
         return DealActionCount(ctx, d[0], d[1] + 86400, action, pred);
      };
      Add(std::move(f));
   }

   void TradeRangeS(const char* name, const char* label, const char* return_type,
                    double (*sel)(const DealRow&))
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "F"; f.category_label = "Deal Aggregations";
      f.source = Source::Deal; f.arity = 2;
      f.return_type = return_type;
      f.supports_predicate = true;
      f.num = [sel](const std::vector<int64_t>& d, const Predicate* pred, const EvalContext& ctx) -> double {
         return TradeRangeSum(ctx, d[0], d[1] + 86400, sel, pred);
      };
      Add(std::move(f));
   }

   void DealCount(const char* name, const char* label, bool (*pred)(const DealRow&))
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "F"; f.category_label = "Deal Aggregations";
      f.source = Source::Deal; f.arity = 2;
      f.return_type = "int";
      f.supports_predicate = true;
      f.num = [pred](const std::vector<int64_t>& d, const Predicate* up, const EvalContext& ctx) -> double {
         return DealRangeCount(ctx, d[0], d[1] + 86400, pred, up);
      };
      Add(std::move(f));
   }

   //--- Position aggregations (category G). source = Position, arity 0.
   void PosSum(const char* name, const char* label, const char* return_type,
               bool (*pred)(const PositionRow&), double (*sel)(const PositionRow&))
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "G"; f.category_label = "Open Positions";
      f.source = Source::Position; f.arity = 0;
      f.return_type = return_type;
      f.supports_predicate = true;
      f.num = [pred, sel](const std::vector<int64_t>&, const Predicate* up, const EvalContext& ctx) -> double {
         return PositionSum(ctx, pred, sel, up);
      };
      Add(std::move(f));
   }

   void PosCnt(const char* name, const char* label, bool (*pred)(const PositionRow&))
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "G"; f.category_label = "Open Positions";
      f.source = Source::Position; f.arity = 0;
      f.return_type = "int";
      f.supports_predicate = true;
      f.num = [pred](const std::vector<int64_t>&, const Predicate* up, const EvalContext& ctx) -> double {
         return PositionCount(ctx, pred, up);
      };
      Add(std::move(f));
   }

   //--- Open order aggregations (category H).
   void OrdOpSum(const char* name, const char* label, const char* return_type,
                 double (*sel)(const OpenOrderRow&))
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "H"; f.category_label = "Open Orders";
      f.source = Source::OrderOpen; f.arity = 0;
      f.return_type = return_type;
      f.supports_predicate = true;
      f.num = [sel](const std::vector<int64_t>&, const Predicate* up, const EvalContext& ctx) -> double {
         return OpenOrderSum(ctx, sel, up);
      };
      Add(std::move(f));
   }

   //--- Order history aggregations (category I).
   void OrdHistCount(const char* name, const char* label, bool (*pred)(const HistoryOrderRow&))
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "I"; f.category_label = "Order History";
      f.source = Source::OrderHist; f.arity = 2;
      f.return_type = "int";
      f.supports_predicate = true;
      f.num = [pred](const std::vector<int64_t>& d, const Predicate* up, const EvalContext& ctx) -> double {
         return OrderHistCount(ctx, d[0], d[1] + 86400, pred, up);
      };
      Add(std::move(f));
   }

   void OrdHistSum(const char* name, const char* label, const char* return_type,
                   double (*sel)(const HistoryOrderRow&))
   {
      Field f;
      f.name = name; f.label = label;
      f.category = "I"; f.category_label = "Order History";
      f.source = Source::OrderHist; f.arity = 2;
      f.return_type = return_type;
      f.supports_predicate = true;
      f.num = [sel](const std::vector<int64_t>& d, const Predicate* up, const EvalContext& ctx) -> double {
         return OrderHistSum(ctx, d[0], d[1] + 86400, sel, up);
      };
      Add(std::move(f));
   }

   //--- Predicates used several times.
   bool IsBuySell(const DealRow& d) { return d.action == IMTDeal::DEAL_BUY || d.action == IMTDeal::DEAL_SELL; }
   bool IsBuy(const DealRow& d) { return d.action == IMTDeal::DEAL_BUY; }
   bool IsSell(const DealRow& d) { return d.action == IMTDeal::DEAL_SELL; }
   bool PosBuy(const PositionRow& p)  { return p.action == IMTPosition::POSITION_BUY; }
   bool PosSell(const PositionRow& p) { return p.action == IMTPosition::POSITION_SELL; }
   bool PosAny(const PositionRow&) { return true; }

   //--- One-time initialization of the catalog.
   void InitCatalog()
   {
      //--- A : Identity (IMTUser strings) ----------------------------
      IdNumeric("login", "Login",
                [](const EvalContext& c) -> double { return (double)Need(c.user,"user").login; },
                [](const EvalContext& c) -> std::string { return std::to_string(Need(c.user,"user").login); });
      Id("group",          "Group",          [](const EvalContext& c) { return Need(c.user,"user").group; });
      Id("name",           "Name",           [](const EvalContext& c) { return Need(c.user,"user").name; });
      Id("first_name",     "First Name",     [](const EvalContext& c) { return Need(c.user,"user").first_name; });
      Id("last_name",      "Last Name",      [](const EvalContext& c) { return Need(c.user,"user").last_name; });
      Id("middle_name",    "Middle Name",    [](const EvalContext& c) { return Need(c.user,"user").middle_name; });
      Id("email",          "Email",          [](const EvalContext& c) { return Need(c.user,"user").email; });
      Id("phone",          "Phone",          [](const EvalContext& c) { return Need(c.user,"user").phone; });
      Id("country",        "Country",        [](const EvalContext& c) { return Need(c.user,"user").country; });
      Id("state",          "State",          [](const EvalContext& c) { return Need(c.user,"user").state; });
      Id("city",           "City",           [](const EvalContext& c) { return Need(c.user,"user").city; });
      Id("zip_code",       "Zip Code",       [](const EvalContext& c) { return Need(c.user,"user").zip_code; });
      Id("address",        "Address",        [](const EvalContext& c) { return Need(c.user,"user").address; });
      Id("id",             "ID",             [](const EvalContext& c) { return Need(c.user,"user").id; });
      Id("company",        "Company",        [](const EvalContext& c) { return Need(c.user,"user").company; });
      Id("account_tag",    "Account Tag",    [](const EvalContext& c) { return Need(c.user,"user").account_tag; });
      Id("status",         "Status",         [](const EvalContext& c) { return Need(c.user,"user").status; });
      Id("comment",        "Comment",        [](const EvalContext& c) { return Need(c.user,"user").comment; });
      Id("lead_campaign",  "Lead Campaign",  [](const EvalContext& c) { return Need(c.user,"user").lead_campaign; });
      Id("lead_source",    "Lead Source",    [](const EvalContext& c) { return Need(c.user,"user").lead_source; });
      Id("last_ip",        "Last IP",        [](const EvalContext& c) { return Need(c.user,"user").last_ip; });
      Id("currency",       "Currency",       [](const EvalContext& c) {
            if(c.daily && !c.daily->empty()) return c.daily->front().currency;
            return std::string();
         });

      //--- B : User Static Numeric -----------------------------------
      UserNum("user_balance",                  "User Balance",          "money", [](const UserInfo& u){ return u.balance; });
      UserNum("user_credit",                   "User Credit",           "money", [](const UserInfo& u){ return u.credit; });
      UserNum("user_leverage",                 "User Leverage",         "int",   [](const UserInfo& u){ return (double)u.leverage; });
      UserNum("user_interest_rate",            "User Interest Rate",    "pct",   [](const UserInfo& u){ return u.interest_rate; });
      UserNum("user_limit_orders",             "User Limit Orders",     "int",   [](const UserInfo& u){ return (double)u.limit_orders; });
      UserNum("user_limit_positions_value",    "User Limit Pos Value",  "money", [](const UserInfo& u){ return u.limit_positions_value; });
      UserNum("user_commission_daily",         "User Commission Daily", "money", [](const UserInfo& u){ return u.commission_daily; });
      UserNum("user_commission_monthly",       "User Commission Monthly","money",[](const UserInfo& u){ return u.commission_monthly; });
      UserNum("user_commission_agent_daily",   "User Agent Commission Daily",  "money", [](const UserInfo& u){ return u.commission_agent_daily; });
      UserNum("user_commission_agent_monthly", "User Agent Commission Monthly","money", [](const UserInfo& u){ return u.commission_agent_monthly; });
      UserNum("user_balance_prev_day",         "User Balance Prev Day", "money", [](const UserInfo& u){ return u.balance_prev_day; });
      UserNum("user_balance_prev_month",       "User Balance Prev Month","money",[](const UserInfo& u){ return u.balance_prev_month; });
      UserNum("user_equity_prev_day",          "User Equity Prev Day",  "money", [](const UserInfo& u){ return u.equity_prev_day; });
      UserNum("user_equity_prev_month",        "User Equity Prev Month","money", [](const UserInfo& u){ return u.equity_prev_month; });
      UserNum("user_registration",             "User Registration",     "date",  [](const UserInfo& u){ return (double)u.registration; });
      UserNum("user_last_access",              "User Last Access",      "date",  [](const UserInfo& u){ return (double)u.last_access; });
      UserNum("user_last_pass_change",         "User Last Pass Change", "date",  [](const UserInfo& u){ return (double)u.last_pass_change; });
      UserNum("user_rights",                   "User Rights",           "int",   [](const UserInfo& u){ return (double)u.rights; });
      UserNum("user_agent",                    "User Agent",            "int",   [](const UserInfo& u){ return (double)u.agent; });
      UserNum("user_client_id",                "User Client ID",        "int",   [](const UserInfo& u){ return (double)u.client_id; });
      UserNum("user_language",                 "User Language",         "int",   [](const UserInfo& u){ return (double)u.language; });

      //--- C : Live Account Snapshot ---------------------------------
      Acc("acc_balance",            "Account Balance",            "money", [](const AccountInfo& a){ return a.balance; });
      Acc("acc_credit",             "Account Credit",             "money", [](const AccountInfo& a){ return a.credit; });
      Acc("acc_equity",             "Account Equity",             "money", [](const AccountInfo& a){ return a.equity; });
      Acc("acc_floating",           "Account Floating",           "money", [](const AccountInfo& a){ return a.floating; });
      Acc("acc_profit",             "Account Profit",             "money", [](const AccountInfo& a){ return a.profit; });
      Acc("acc_storage",            "Account Storage",            "money", [](const AccountInfo& a){ return a.storage; });
      Acc("acc_margin",             "Account Margin",             "money", [](const AccountInfo& a){ return a.margin; });
      Acc("acc_margin_free",        "Account Margin Free",        "money", [](const AccountInfo& a){ return a.margin_free; });
      Acc("acc_margin_level",       "Account Margin Level",       "pct",   [](const AccountInfo& a){ return a.margin_level; });
      Acc("acc_margin_initial",     "Account Margin Initial",     "money", [](const AccountInfo& a){ return a.margin_initial; });
      Acc("acc_margin_maintenance", "Account Margin Maintenance", "money", [](const AccountInfo& a){ return a.margin_maintenance; });
      Acc("acc_assets",             "Account Assets",             "money", [](const AccountInfo& a){ return a.assets; });
      Acc("acc_liabilities",        "Account Liabilities",        "money", [](const AccountInfo& a){ return a.liabilities; });
      Acc("acc_blocked_commission", "Account Blocked Commission", "money", [](const AccountInfo& a){ return a.blocked_commission; });
      Acc("acc_blocked_profit",     "Account Blocked Profit",     "money", [](const AccountInfo& a){ return a.blocked_profit; });
      Acc("acc_so_level",           "Account SO Level",           "pct",   [](const AccountInfo& a){ return a.so_level; });
      Acc("acc_so_equity",          "Account SO Equity",          "money", [](const AccountInfo& a){ return a.so_equity; });
      Acc("acc_so_margin",          "Account SO Margin",          "money", [](const AccountInfo& a){ return a.so_margin; });

      //--- D : Daily Snapshot Start/End -------------------------------
      DailyPair("equity",             "Equity",             "money", [](const DailyRow& r) { return r.profit_equity; });
      DailyPair("balance",            "Balance",            "money", [](const DailyRow& r) { return r.balance; });
      DailyPair("credit",             "Credit",             "money", [](const DailyRow& r) { return r.credit; });
      DailyPair("floating",           "Floating",           "money", [](const DailyRow& r) { return r.profit; });
      DailyPair("margin",             "Margin",             "money", [](const DailyRow& r) { return r.margin; });
      DailyPair("margin_free",        "Margin Free",        "money", [](const DailyRow& r) { return r.margin_free; });
      DailyPair("margin_level",       "Margin Level",       "pct",   [](const DailyRow& r) { return r.margin_level; });
      DailyPair("margin_leverage",    "Margin Leverage",    "int",   [](const DailyRow& r) { return (double)r.margin_leverage; });
      DailyPair("interest_rate",      "Interest Rate",      "pct",   [](const DailyRow& r) { return r.interest_rate; });
      DailyPair("profit_storage",     "Profit Storage",     "money", [](const DailyRow& r) { return r.profit_storage; });
      DailyPair("profit_assets",      "Profit Assets",      "money", [](const DailyRow& r) { return r.profit_assets; });
      DailyPair("profit_liabilities", "Profit Liabilities", "money", [](const DailyRow& r) { return r.profit_liabilities; });
      DailyPair("balance_prev_day",   "Balance Prev Day",   "money", [](const DailyRow& r) { return r.balance_prev_day; });
      DailyPair("balance_prev_month", "Balance Prev Month", "money", [](const DailyRow& r) { return r.balance_prev_month; });
      DailyPair("equity_prev_day",    "Equity Prev Day",    "money", [](const DailyRow& r) { return r.equity_prev_day; });
      DailyPair("equity_prev_month",  "Equity Prev Month",  "money", [](const DailyRow& r) { return r.equity_prev_month; });

      //--- E : Daily Δ Range Sums ------------------------------------
      DailyRangeS("sum_daily_profit",       "Σ Daily Profit (range)",       "money", [](const DailyRow& r){ return r.daily_profit; });
      DailyRangeS("sum_daily_balance",      "Σ Daily Balance (range)",      "money", [](const DailyRow& r){ return r.daily_balance; });
      DailyRangeS("sum_daily_credit",       "Σ Daily Credit (range)",       "money", [](const DailyRow& r){ return r.daily_credit; });
      DailyRangeS("sum_daily_charge",       "Σ Daily Charge (range)",       "money", [](const DailyRow& r){ return r.daily_charge; });
      DailyRangeS("sum_daily_correction",   "Σ Daily Correction (range)",   "money", [](const DailyRow& r){ return r.daily_correction; });
      DailyRangeS("sum_daily_bonus",        "Σ Daily Bonus (range)",        "money", [](const DailyRow& r){ return r.daily_bonus; });
      DailyRangeS("sum_daily_storage",      "Σ Daily Storage (range)",      "money", [](const DailyRow& r){ return r.daily_storage; });
      DailyRangeS("sum_daily_comm_instant", "Σ Daily Comm Instant (range)", "money", [](const DailyRow& r){ return r.daily_comm_instant; });
      DailyRangeS("sum_daily_comm_round",   "Σ Daily Comm Round (range)",   "money", [](const DailyRow& r){ return r.daily_comm_round; });
      DailyRangeS("sum_daily_agent",        "Σ Daily Agent (range)",        "money", [](const DailyRow& r){ return r.daily_agent; });
      DailyRangeS("sum_daily_interest",     "Σ Daily Interest (range)",     "money", [](const DailyRow& r){ return r.daily_interest; });

      //--- F : Deal Aggregations ------------------------------------
      //--- Bucket sums (1=deposit, -1=withdrawal, 2=writeoff, 3=adjustment)
      DealBucketField("sum_deposit",    "Σ Deposit",           1,  /*abs*/true,  /*negate*/false);
      DealBucketField("sum_withdrawal", "Σ Withdrawal (neg)", -1,  /*abs*/true,  /*negate*/true);
      DealBucketField("sum_writeoff",   "Σ Balance Writeoff",  2,  /*abs*/false, /*negate*/false);
      DealBucketField("sum_adjustment", "Σ Trade Adjustments", 3,  /*abs*/false, /*negate*/false);
      DealBucketCnt  ("count_deposits",   "# Deposits",     1);
      DealBucketCnt  ("count_withdrawals","# Withdrawals", -1);

      //--- Raw per-DEAL_* aggregations (bypass deposit/withdrawal bucket;
      //--- one row type per field; combine with predicates for free filtering).
      DealActionField("sum_balance",             "Σ Balance (DEAL_BALANCE, signed)",         IMTDeal::DEAL_BALANCE,            /*abs*/false);
      DealActionField("sum_balance_abs",         "Σ |Balance| (DEAL_BALANCE)",               IMTDeal::DEAL_BALANCE,            /*abs*/true);
      DealActionField("sum_credit",              "Σ Credit (DEAL_CREDIT)",                   IMTDeal::DEAL_CREDIT,             /*abs*/false);
      DealActionField("sum_charge",              "Σ Charge (DEAL_CHARGE)",                   IMTDeal::DEAL_CHARGE,             /*abs*/false);
      DealActionField("sum_correction",          "Σ Correction (DEAL_CORRECTION)",           IMTDeal::DEAL_CORRECTION,         /*abs*/false);
      DealActionField("sum_bonus",               "Σ Bonus (DEAL_BONUS)",                     IMTDeal::DEAL_BONUS,              /*abs*/false);
      DealActionField("sum_commission_deal",     "Σ Commission (DEAL_COMMISSION)",           IMTDeal::DEAL_COMMISSION,         /*abs*/false);
      DealActionField("sum_commission_daily",    "Σ Commission daily (DEAL_COMMISSION_DAILY)",   IMTDeal::DEAL_COMMISSION_DAILY,   /*abs*/false);
      DealActionField("sum_commission_monthly",  "Σ Commission monthly (DEAL_COMMISSION_MONTHLY)", IMTDeal::DEAL_COMMISSION_MONTHLY, /*abs*/false);
      DealActionField("sum_agent",               "Σ Agent (DEAL_AGENT)",                     IMTDeal::DEAL_AGENT,              /*abs*/false);
      DealActionField("sum_agent_daily",         "Σ Agent daily (DEAL_AGENT_DAILY)",         IMTDeal::DEAL_AGENT_DAILY,        /*abs*/false);
      DealActionField("sum_agent_monthly",       "Σ Agent monthly (DEAL_AGENT_MONTHLY)",     IMTDeal::DEAL_AGENT_MONTHLY,      /*abs*/false);
      DealActionField("sum_interest",            "Σ Interest rate (DEAL_INTERESTRATE)",      IMTDeal::DEAL_INTERESTRATE,       /*abs*/false);
      DealActionField("sum_dividend",            "Σ Dividend (DEAL_DIVIDEND)",               IMTDeal::DEAL_DIVIDEND,           /*abs*/false);
      DealActionField("sum_dividend_franked",    "Σ Dividend franked (DEAL_DIVIDEND_FRANKED)", IMTDeal::DEAL_DIVIDEND_FRANKED, /*abs*/false);
      DealActionField("sum_tax",                 "Σ Tax (DEAL_TAX)",                         IMTDeal::DEAL_TAX,                /*abs*/false);

      DealActionCnt  ("count_balance",           "# DEAL_BALANCE rows",                      IMTDeal::DEAL_BALANCE);
      DealActionCnt  ("count_credit",            "# DEAL_CREDIT rows",                       IMTDeal::DEAL_CREDIT);
      DealActionCnt  ("count_charge",            "# DEAL_CHARGE rows",                       IMTDeal::DEAL_CHARGE);
      DealActionCnt  ("count_correction",        "# DEAL_CORRECTION rows",                   IMTDeal::DEAL_CORRECTION);
      DealActionCnt  ("count_bonus",             "# DEAL_BONUS rows",                        IMTDeal::DEAL_BONUS);
      DealActionCnt  ("count_dividend",          "# DEAL_DIVIDEND rows",                     IMTDeal::DEAL_DIVIDEND);
      DealActionCnt  ("count_tax",               "# DEAL_TAX rows",                          IMTDeal::DEAL_TAX);
      //--- Closed-trade sums
      TradeRangeS("sum_closed_pl",   "Σ Closed P/L",      "money", [](const DealRow& d){ return d.profit; });
      TradeRangeS("sum_profit_raw",  "Σ Profit Raw",      "money", [](const DealRow& d){ return d.profit_raw; });
      TradeRangeS("sum_commission",  "Σ Commission",      "money", [](const DealRow& d){ return d.commission; });
      TradeRangeS("sum_swap",        "Σ Swap (storage)",  "money", [](const DealRow& d){ return d.storage; });
      TradeRangeS("sum_fee",         "Σ Fee",             "money", [](const DealRow& d){ return d.fee; });
      TradeRangeS("sum_value",       "Σ Deal Value",      "money", [](const DealRow& d){ return d.value; });
      TradeRangeS("sum_volume",      "Σ Volume",          "int",   [](const DealRow& d){ return (double)d.volume; });
      TradeRangeS("sum_volume_ext",  "Σ Volume Ext",      "int",   [](const DealRow& d){ return (double)d.volume_ext; });
      //--- Lots (symbol-independent: volume_ext / 1e8). Entry-filtered so the
      //--- opened and closed legs don't double up. Predicate-friendly so users
      //--- can further filter by action / symbol / trade_lifetime / comment.
      {
         Field f; f.name = "sum_lots_closed"; f.label = "Σ Lots closed";
         f.category = "F"; f.category_label = "Deal Aggregations";
         f.source = Source::Deal; f.arity = 2; f.return_type = "money";
         f.supports_predicate = true;
         f.num = [](const std::vector<int64_t>& d, const Predicate* pred, const EvalContext& ctx) -> double {
            return TradeLotsSum(ctx, d[0], d[1] + 86400, /*out_only*/true, pred);
         };
         Add(std::move(f));
      }
      {
         Field f; f.name = "sum_lots_opened"; f.label = "Σ Lots opened";
         f.category = "F"; f.category_label = "Deal Aggregations";
         f.source = Source::Deal; f.arity = 2; f.return_type = "money";
         f.supports_predicate = true;
         f.num = [](const std::vector<int64_t>& d, const Predicate* pred, const EvalContext& ctx) -> double {
            return TradeLotsSum(ctx, d[0], d[1] + 86400, /*out_only*/false, pred);
         };
         Add(std::move(f));
      }
      //--- Counts
      DealCount("count_deals",          "# Deals (any)",       [](const DealRow&){ return true; });
      DealCount("count_closed_trades",  "# Closed Trades",     IsBuySell);
      DealCount("count_buy_deals",      "# Buy Deals",         IsBuy);
      DealCount("count_sell_deals",     "# Sell Deals",        IsSell);

      //--- G : Open Positions -----------------------------------------
      PosCnt("position_count",            "# Open Positions",        PosAny);
      PosCnt("position_count_buy",        "# Open BUY Positions",    PosBuy);
      PosCnt("position_count_sell",       "# Open SELL Positions",   PosSell);
      PosSum("position_volume_sum",       "Σ Open Position Volume",       "int",   PosAny,  [](const PositionRow& p){ return (double)p.volume; });
      PosSum("position_volume_buy_sum",   "Σ Open BUY Volume",            "int",   PosBuy,  [](const PositionRow& p){ return (double)p.volume; });
      PosSum("position_volume_sell_sum",  "Σ Open SELL Volume",           "int",   PosSell, [](const PositionRow& p){ return (double)p.volume; });
      PosSum("position_profit_sum",       "Σ Open Position Profit",       "money", PosAny,  [](const PositionRow& p){ return p.profit; });
      PosSum("position_storage_sum",      "Σ Open Position Storage",      "money", PosAny,  [](const PositionRow& p){ return p.storage; });

      //--- H : Open Orders -------------------------------------------
      {
         Field f;
         f.name = "order_open_count"; f.label = "# Open Orders";
         f.category = "H"; f.category_label = "Open Orders";
         f.source = Source::OrderOpen; f.arity = 0; f.return_type = "int";
         f.supports_predicate = true;
         f.num = [](const std::vector<int64_t>&, const Predicate* up, const EvalContext& ctx) { return OpenOrderCount(ctx, up); };
         Add(std::move(f));
      }
      OrdOpSum("order_open_volume_initial_sum", "Σ Open Order Volume (initial)", "int", [](const OpenOrderRow& o){ return (double)o.volume_initial; });
      OrdOpSum("order_open_volume_current_sum", "Σ Open Order Volume (current)", "int", [](const OpenOrderRow& o){ return (double)o.volume_current; });

      //--- I : Order History ----------------------------------------
      OrdHistCount("count_orders_history",  "# Orders (history)",        [](const HistoryOrderRow&){ return true; });
      OrdHistCount("count_orders_filled",   "# Orders Filled",           [](const HistoryOrderRow& o){ return o.state == IMTOrder::ORDER_STATE_FILLED; });
      OrdHistCount("count_orders_canceled", "# Orders Canceled",         [](const HistoryOrderRow& o){ return o.state == IMTOrder::ORDER_STATE_CANCELED; });
      OrdHistCount("count_orders_expired",  "# Orders Expired",          [](const HistoryOrderRow& o){ return o.state == IMTOrder::ORDER_STATE_EXPIRED; });
      OrdHistSum  ("sum_order_history_volume_initial", "Σ Hist Order Volume (initial)", "int", [](const HistoryOrderRow& o){ return (double)o.volume_initial; });
      OrdHistSum  ("sum_order_history_volume_filled",  "Σ Hist Order Volume (filled)",  "int",
                   [](const HistoryOrderRow& o){ return (double)(o.volume_initial - o.volume_current); });
   }
}

const std::vector<Field>& FieldCatalog::All()
{
   static bool init = false;
   if(!init) { InitCatalog(); init = true; }
   return Mutable();
}

const Field* FieldCatalog::Lookup(const std::string& name)
{
   for(const auto& f : All())
      if(f.name == name) return &f;
   return nullptr;
}

double FieldCatalog::EvaluateNumeric(const std::string& name,
                                     const std::vector<std::string>& args,
                                     const Predicate* predicate,
                                     const EvalContext& ctx)
{
   const Field* f = Lookup(name);
   if(!f) throw std::runtime_error("unknown field: " + name);
   if(!f->num) throw std::runtime_error("field '" + name + "' is text-only, not usable in expressions");
   if((int)args.size() != f->arity)
      throw std::runtime_error("field '" + name + "' expects " + std::to_string(f->arity)
                               + " date args, got " + std::to_string(args.size()));
   if(predicate && !f->supports_predicate)
      throw std::runtime_error("field '" + name + "' does not support per-row filters");
   std::vector<int64_t> resolved; resolved.reserve(args.size());
   for(const auto& a : args)
   {
      if(!ctx.date_params) throw std::runtime_error("date_params unbound for field " + name);
      auto it = ctx.date_params->find(a);
      if(it == ctx.date_params->end())
         throw std::runtime_error("date param '" + a + "' not bound for field " + name);
      resolved.push_back(it->second);
   }
   return f->num(resolved, predicate, ctx);
}

std::string FieldCatalog::EvaluateText(const std::string& name, const EvalContext& ctx)
{
   const Field* f = Lookup(name);
   if(!f) throw std::runtime_error("unknown identifier: " + name);
   if(f->txt) return f->txt(ctx);
   if(f->num) { double v = f->num({}, nullptr, ctx); char buf[32]; snprintf(buf, sizeof(buf), "%.4f", v); return buf; }
   throw std::runtime_error("field '" + name + "' has no text or numeric value");
}

namespace
{
   void CollectAstSources(const ExprNode& n, AnalyzeResult& out)
   {
      if(n.type == ExprNode::Type::Literal) return;
      if(n.type == ExprNode::Type::BinOp)
      {
         if(n.left)  CollectAstSources(*n.left,  out);
         if(n.right) CollectAstSources(*n.right, out);
         return;
      }
      // Field
      const Field* f = Lookup(n.field_name);
      if(!f) return;  // validation catches this
      out.sources.insert(f->source);
      if(f->arity == 1 && n.field_args.size() == 1)
         out.snapshot_date_params.insert(n.field_args[0]);
      else if(f->arity == 2 && n.field_args.size() == 2)
      {
         out.range_date_params.insert(n.field_args[0]);
         out.range_date_params.insert(n.field_args[1]);
      }
   }
}

AnalyzeResult FieldCatalog::Analyze(const ReportTemplate& tpl)
{
   AnalyzeResult r;
   //--- Identifier columns also need User source (always loaded anyway).
   bool any_identifier = false;
   for(const auto& c : tpl.columns)
   {
      if(c.kind == ColumnSpec::Kind::Identifier)
      {
         any_identifier = true;
         //--- text identifiers: their source is User; numeric currency falls back too.
         const Field* f = Lookup(c.source);
         if(f) r.sources.insert(f->source);
      }
      else if(c.expr)
      {
         CollectAstSources(*c.expr, r);
      }
   }
   if(any_identifier) r.sources.insert(Source::User);
   return r;
}

namespace
{
   //--- forward declaration: predicate validator (defined below ValidateExpr)
   void ValidatePredicate(const Predicate& p, FieldCatalog::Source src,
                          const std::string& path,
                          std::vector<FieldCatalog::ValidationError>* errs);

   void ValidateExpr(const ExprNode& n, const std::set<std::string>& date_params,
                     int depth, const std::string& path,
                     std::vector<ValidationError>* errs)
   {
      if(depth > Expression::kMaxDepth)
      {
         errs->push_back({ path, "expression nesting exceeds " + std::to_string(Expression::kMaxDepth) });
         return;
      }
      if(n.type == ExprNode::Type::Literal) return;
      if(n.type == ExprNode::Type::BinOp)
      {
         if(n.op != '+' && n.op != '-' && n.op != '*' && n.op != '/')
            errs->push_back({ path, std::string("invalid op '") + n.op + "'" });
         if(!n.left)  errs->push_back({ path + ".left",  "missing left operand" });
         else         ValidateExpr(*n.left,  date_params, depth + 1, path + ".left",  errs);
         if(!n.right) errs->push_back({ path + ".right", "missing right operand" });
         else         ValidateExpr(*n.right, date_params, depth + 1, path + ".right", errs);
         return;
      }
      // Field
      const Field* f = Lookup(n.field_name);
      if(!f) { errs->push_back({ path, "unknown field '" + n.field_name + "'" }); return; }
      if(f->return_type == "text" || !f->num)
      {
         errs->push_back({ path, "field '" + n.field_name + "' is text-only and cannot appear in an expression" });
         return;
      }
      if((int)n.field_args.size() != f->arity)
      {
         errs->push_back({ path, "field '" + n.field_name + "' expects " + std::to_string(f->arity)
                                  + " date args, got " + std::to_string(n.field_args.size()) });
         return;
      }
      for(const auto& a : n.field_args)
         if(!date_params.count(a))
            errs->push_back({ path, "field '" + n.field_name + "' uses date param '" + a + "' not in date_params" });

      if(n.predicate)
      {
         if(!f->supports_predicate)
         {
            errs->push_back({ path, "field '" + n.field_name + "' does not support per-row filters" });
            return;
         }
         ValidatePredicate(*n.predicate, f->source, path + ".predicate", errs);
      }
   }

   //--- Recursively validate a predicate against a source's filterable schema.
   void ValidatePredicate(const Predicate& p, FieldCatalog::Source src,
                          const std::string& path,
                          std::vector<FieldCatalog::ValidationError>* errs)
   {
      if(p.kind == Predicate::Kind::And || p.kind == Predicate::Kind::Or)
      {
         const char* k = p.kind == Predicate::Kind::And ? "and" : "or";
         if(p.children.empty()) errs->push_back({ path, std::string(k) + " has no children" });
         for(size_t i = 0; i < p.children.size(); ++i)
            if(p.children[i]) ValidatePredicate(*p.children[i], src,
                                                 path + "." + k + "[" + std::to_string(i) + "]", errs);
            else errs->push_back({ path + "." + k + "[" + std::to_string(i) + "]", "null child" });
         return;
      }
      if(p.kind == Predicate::Kind::Not)
      {
         if(!p.child) errs->push_back({ path + ".not", "missing child of NOT" });
         else         ValidatePredicate(*p.child, src, path + ".not", errs);
         return;
      }
      //--- Cmp leaf
      const auto& list = FieldCatalog::FilterableFor(src);
      const FieldCatalog::FilterableField* ff = nullptr;
      for(const auto& x : list) if(x.name == p.cmp.field) { ff = &x; break; }
      if(!ff)
      {
         errs->push_back({ path + ".cmp", "unknown filter field '" + p.cmp.field
                                          + "' for source '" + FieldCatalog::SourceName(src) + "'" });
         return;
      }
      //--- op compatibility check
      const auto op = p.cmp.op;
      const bool is_text_op = (op == FilterOp::Regex || op == FilterOp::Contains
                              || op == FilterOp::StartsWith || op == FilterOp::EndsWith);
      const bool is_num_op  = (op == FilterOp::Lt || op == FilterOp::Lte
                              || op == FilterOp::Gt || op == FilterOp::Gte);
      if(ff->type == FieldCatalog::FilterValueType::Text)
      {
         if(is_num_op) errs->push_back({ path + ".cmp", "op '" + std::string(FilterOpName(op))
                                                       + "' not valid for text field '" + ff->name + "'" });
      }
      else  // Num or Enum
      {
         if(is_text_op) errs->push_back({ path + ".cmp", "op '" + std::string(FilterOpName(op))
                                                       + "' not valid for numeric/enum field '" + ff->name + "'" });
      }
   }
}

std::vector<ValidationError> FieldCatalog::Validate(const ReportTemplate& tpl)
{
   std::vector<ValidationError> errs;
   std::set<std::string> dps(tpl.date_params.begin(), tpl.date_params.end());

   if(tpl.row_model != "per_account")
      errs.push_back({ "row_model", "only 'per_account' is supported in v1" });

   //--- Column-key uniqueness + per-column checks
   std::set<std::string> seen_keys;
   for(size_t i = 0; i < tpl.columns.size(); ++i)
   {
      const auto& c = tpl.columns[i];
      const std::string base = "columns[" + std::to_string(i) + "]";
      if(c.key.empty())       errs.push_back({ base + ".key", "missing key" });
      if(c.label.empty())     errs.push_back({ base + ".label", "missing label" });
      if(!seen_keys.insert(c.key).second)
         errs.push_back({ base + ".key", "duplicate column key '" + c.key + "'" });

      if(c.kind == ColumnSpec::Kind::Identifier)
      {
         if(c.source.empty())
            errs.push_back({ base + ".source", "identifier column must have a source field name" });
         else if(!Lookup(c.source))
            errs.push_back({ base + ".source", "unknown identifier field '" + c.source + "'" });
      }
      else
      {
         if(!c.expr) errs.push_back({ base + ".expr", "formula column missing expression" });
         else        ValidateExpr(*c.expr, dps, 1, base + ".expr", &errs);
      }
   }

   //--- Sort key references an existing column
   if(!tpl.sort.column_key.empty())
   {
      bool found = false;
      for(const auto& c : tpl.columns) if(c.key == tpl.sort.column_key) { found = true; break; }
      if(!found) errs.push_back({ "sort.column_key", "sort column '" + tpl.sort.column_key + "' is not in columns[]" });
   }

   return errs;
}

std::vector<FieldCatalog::ValidationError>
FieldCatalog::ValidatePredicateStandalone(const Predicate& p, Source src)
{
   std::vector<ValidationError> errs;
   ValidatePredicate(p, src, "predicate", &errs);
   return errs;
}

//+--- Filterable subcatalog ------------------------------------------+

namespace
{
   using FF = FilterableField;
   using VT = FilterValueType;

   std::vector<FF>& MutableFilterable(Source s)
   {
      static std::vector<FF> deal, daily, position, order_open, order_hist, user, none;
      switch(s)
      {
         case Source::Deal:      return deal;
         case Source::Daily:     return daily;
         case Source::Position:  return position;
         case Source::OrderOpen: return order_open;
         case Source::OrderHist: return order_hist;
         case Source::User:      return user;
         default:                return none;
      }
   }

   void InitFilterable()
   {
      //--- Deal ---
      auto& deal = MutableFilterable(Source::Deal);
      deal.push_back({"comment",      "Comment",      VT::Text, {}});
      deal.push_back({"symbol",       "Symbol",       VT::Text, {}});
      deal.push_back({"gateway",      "Gateway",      VT::Text, {}});
      deal.push_back({"external_id",  "External ID",  VT::Text, {}});
      deal.push_back({"action",       "Action",       VT::Enum, {
         {0, "BUY"}, {1, "SELL"}, {2, "BALANCE"}, {3, "CREDIT"}, {4, "CHARGE"},
         {5, "CORRECTION"}, {6, "BONUS"}, {7, "COMMISSION"}, {8, "COMMISSION_DAILY"},
         {9, "COMMISSION_MONTHLY"}, {10, "AGENT_DAILY"}, {11, "AGENT_MONTHLY"},
         {12, "INTEREST_RATE"}, {13, "BUY_CANCELED"}, {14, "SELL_CANCELED"}, {15, "DIVIDEND"},
         {16, "DIVIDEND_FRANKED"}, {17, "TAX"}, {18, "AGENT"}, {19, "SO_COMPENSATION"},
      }});
      deal.push_back({"entry", "Entry", VT::Enum, {
         {0, "IN"}, {1, "OUT"}, {2, "INOUT"}, {3, "OUT_BY"},
      }});
      deal.push_back({"reason", "Reason", VT::Enum, {
         {0, "CLIENT"}, {1, "EXPERT"}, {2, "DEALER"}, {3, "SIGNAL"}, {4, "GATEWAY"},
         {5, "MOBILE"}, {6, "WEB"}, {7, "API"},
      }});
      deal.push_back({"profit",        "Profit",        VT::Num, {}});
      deal.push_back({"profit_raw",    "Profit Raw",    VT::Num, {}});
      deal.push_back({"storage",       "Storage (swap)",VT::Num, {}});
      deal.push_back({"commission",    "Commission",    VT::Num, {}});
      deal.push_back({"fee",           "Fee",           VT::Num, {}});
      deal.push_back({"value",         "Value",         VT::Num, {}});
      deal.push_back({"volume",        "Volume",        VT::Num, {}});
      deal.push_back({"volume_ext",    "Volume Ext",    VT::Num, {}});
      deal.push_back({"volume_closed", "Volume Closed", VT::Num, {}});
      deal.push_back({"price",         "Price",         VT::Num, {}});
      deal.push_back({"time",          "Time (Unix s)", VT::Num, {}});
      //--- Derived: seconds between matching IN deal and this OUT deal in the
      //--- fetched window. 0 for IN/INOUT rows or OUTs with no matching IN.
      deal.push_back({"trade_lifetime","Trade lifetime (s)", VT::Num, {}});

      //--- Daily ---
      auto& daily = MutableFilterable(Source::Daily);
      daily.push_back({"balance",          "Balance",          VT::Num, {}});
      daily.push_back({"credit",           "Credit",           VT::Num, {}});
      daily.push_back({"profit_equity",    "Equity",           VT::Num, {}});
      daily.push_back({"profit",           "Floating",         VT::Num, {}});
      daily.push_back({"margin",           "Margin",           VT::Num, {}});
      daily.push_back({"margin_free",      "Margin Free",      VT::Num, {}});
      daily.push_back({"margin_level",     "Margin Level",     VT::Num, {}});
      daily.push_back({"daily_balance",    "Daily Balance",    VT::Num, {}});
      daily.push_back({"daily_credit",     "Daily Credit",     VT::Num, {}});
      daily.push_back({"daily_profit",     "Daily Profit",     VT::Num, {}});
      daily.push_back({"daily_charge",     "Daily Charge",     VT::Num, {}});
      daily.push_back({"daily_correction", "Daily Correction", VT::Num, {}});
      daily.push_back({"daily_bonus",      "Daily Bonus",      VT::Num, {}});
      daily.push_back({"daily_storage",    "Daily Storage",    VT::Num, {}});
      daily.push_back({"daily_agent",      "Daily Agent",      VT::Num, {}});
      daily.push_back({"daily_interest",   "Daily Interest",   VT::Num, {}});
      daily.push_back({"datetime",         "Datetime",         VT::Num, {}});

      //--- Position ---
      auto& pos = MutableFilterable(Source::Position);
      pos.push_back({"symbol",      "Symbol",      VT::Text, {}});
      pos.push_back({"comment",     "Comment",     VT::Text, {}});
      pos.push_back({"action",      "Action",      VT::Enum, { {0, "BUY"}, {1, "SELL"} }});
      pos.push_back({"volume",      "Volume",      VT::Num,  {}});
      pos.push_back({"profit",      "Profit",      VT::Num,  {}});
      pos.push_back({"storage",     "Storage",     VT::Num,  {}});
      pos.push_back({"price_open",  "Price Open",  VT::Num,  {}});
      pos.push_back({"time_create", "Time Create", VT::Num,  {}});

      //--- Open & history orders (same accessor set) ---
      const std::vector<FF> order_common = {
         {"symbol",         "Symbol",         VT::Text, {}},
         {"comment",        "Comment",        VT::Text, {}},
         {"external_id",    "External ID",    VT::Text, {}},
         {"state",          "State",          VT::Enum, {
            {0,"STARTED"}, {1,"PLACED"}, {2,"CANCELED"}, {3,"PARTIAL"},
            {4,"FILLED"},  {5,"REJECTED"}, {6,"EXPIRED"},
         }},
         {"type",           "Type",           VT::Enum, {
            {0,"BUY"}, {1,"SELL"}, {2,"BUY_LIMIT"}, {3,"SELL_LIMIT"},
            {4,"BUY_STOP"}, {5,"SELL_STOP"}, {6,"BUY_STOP_LIMIT"}, {7,"SELL_STOP_LIMIT"},
            {8,"CLOSE_BY"},
         }},
         {"type_fill",      "Type Fill",      VT::Enum, { {0,"FOK"}, {1,"IOC"}, {2,"RETURN"} }},
         {"type_time",      "Type Time",      VT::Enum, { {0,"GTC"}, {1,"DAY"}, {2,"SPECIFIED"}, {3,"SPECIFIED_DAY"} }},
         {"volume_initial", "Volume Initial", VT::Num,  {}},
         {"volume_current", "Volume Current", VT::Num,  {}},
         {"time_setup",     "Time Setup",     VT::Num,  {}},
         {"time_done",      "Time Done",      VT::Num,  {}},
         {"price_order",    "Price Order",    VT::Num,  {}},
      };
      MutableFilterable(Source::OrderOpen) = order_common;
      MutableFilterable(Source::OrderHist) = order_common;

      //--- User (IMTUser fields, applied as a post-filter after LoadUsers) ---
      auto& user = MutableFilterable(Source::User);
      user.push_back({"group",          "Group",          VT::Text, {}});
      user.push_back({"name",           "Name",           VT::Text, {}});
      user.push_back({"first_name",     "First Name",     VT::Text, {}});
      user.push_back({"last_name",      "Last Name",      VT::Text, {}});
      user.push_back({"middle_name",    "Middle Name",    VT::Text, {}});
      user.push_back({"email",          "Email",          VT::Text, {}});
      user.push_back({"phone",          "Phone",          VT::Text, {}});
      user.push_back({"country",        "Country",        VT::Text, {}});
      user.push_back({"state",          "State",          VT::Text, {}});
      user.push_back({"city",           "City",           VT::Text, {}});
      user.push_back({"zip_code",       "Zip / Postal",   VT::Text, {}});
      user.push_back({"address",        "Address",        VT::Text, {}});
      user.push_back({"id",             "ID",             VT::Text, {}});
      user.push_back({"company",        "Company",        VT::Text, {}});
      user.push_back({"account_tag",    "Account Tag",    VT::Text, {}});
      user.push_back({"status",         "Status",         VT::Text, {}});
      user.push_back({"comment",        "Comment",        VT::Text, {}});
      user.push_back({"lead_campaign",  "Lead Campaign",  VT::Text, {}});
      user.push_back({"lead_source",    "Lead Source",    VT::Text, {}});
      user.push_back({"last_ip",        "Last IP",        VT::Text, {}});
      user.push_back({"agent",            "Agent",            VT::Num, {}});
      user.push_back({"client_id",        "Client ID",        VT::Num, {}});
      user.push_back({"leverage",         "Leverage",         VT::Num, {}});
      user.push_back({"language",         "Language",         VT::Num, {}});
      user.push_back({"rights",           "Rights mask",      VT::Num, {}});
      user.push_back({"balance",          "Current Balance",  VT::Num, {}});
      user.push_back({"credit",           "Current Credit",   VT::Num, {}});
      user.push_back({"interest_rate",    "Interest Rate",    VT::Num, {}});
      user.push_back({"balance_prev_day", "Balance Prev Day", VT::Num, {}});
      user.push_back({"balance_prev_month","Balance Prev Month",VT::Num,{}});
      user.push_back({"equity_prev_day",  "Equity Prev Day",  VT::Num, {}});
      user.push_back({"equity_prev_month","Equity Prev Month",VT::Num, {}});
      user.push_back({"registration",     "Registration",     VT::Num, {}});
      user.push_back({"last_access",      "Last Access",      VT::Num, {}});
      user.push_back({"last_pass_change", "Last Pass Change", VT::Num, {}});
   }
}

const std::vector<FilterableField>& FieldCatalog::FilterableFor(Source s)
{
   static bool init = false;
   if(!init) { InitFilterable(); init = true; }
   return MutableFilterable(s);
}

json FieldCatalog::CatalogToJson()
{
   //--- Group fields by category for the picker.
   std::vector<std::pair<std::string, std::string>> categories = {
      {"A", "Identity"},
      {"B", "User Static Numeric"},
      {"C", "Live Account Snapshot"},
      {"D", "Daily Snapshot Start/End"},
      {"E", "Daily Δ Range Sums"},
      {"F", "Deal Aggregations"},
      {"G", "Open Positions"},
      {"H", "Open Orders"},
      {"I", "Order History"},
   };

   json cats = json::array();
   for(const auto& c : categories)
      cats.push_back({ {"id", c.first}, {"label", c.second} });

   json fs = json::array();
   for(const auto& f : All())
   {
      json j;
      j["name"]               = f.name;
      j["label"]              = f.label;
      j["category"]           = f.category;
      j["source"]             = SourceName(f.source);
      j["arity"]              = f.arity;
      j["return_type"]        = f.return_type;
      j["is_identifier"]      = (f.txt && !f.num);
      j["supports_predicate"] = f.supports_predicate;
      fs.push_back(std::move(j));
   }

   //--- Filterable subcatalog per source.
   auto serializeFilterable = [](Source s) {
      json arr = json::array();
      for(const auto& ff : FieldCatalog::FilterableFor(s))
      {
         json one;
         one["name"]  = ff.name;
         one["label"] = ff.label;
         switch(ff.type)
         {
            case FilterValueType::Num:  one["type"] = "num";  break;
            case FilterValueType::Text: one["type"] = "text"; break;
            case FilterValueType::Enum: one["type"] = "enum"; break;
         }
         if(ff.type == FilterValueType::Enum)
         {
            json evs = json::array();
            for(const auto& kv : ff.enum_values)
               evs.push_back({ {"code", kv.first}, {"label", kv.second} });
            one["enum_values"] = evs;
         }
         arr.push_back(std::move(one));
      }
      return arr;
   };
   json filterable;
   filterable["deal"]       = serializeFilterable(Source::Deal);
   filterable["daily"]      = serializeFilterable(Source::Daily);
   filterable["position"]   = serializeFilterable(Source::Position);
   filterable["order_open"] = serializeFilterable(Source::OrderOpen);
   filterable["order_hist"] = serializeFilterable(Source::OrderHist);
   filterable["user"]       = serializeFilterable(Source::User);

   return json{
      {"categories", cats},
      {"fields", fs},
      {"filterable_by_source", filterable},
   };
}
