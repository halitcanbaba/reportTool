//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     FieldCatalog.h - all template-addressable fields             |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include "EvalContext.h"
#include "../third_party/json.hpp"
#include <string>
#include <vector>
#include <set>
#include <functional>

namespace FieldCatalog
{
   enum class Source
   {
      User,
      Account,
      Daily,
      Deal,
      Position,
      OrderOpen,
      OrderHist,
      Literal
   };

   const char* SourceName(Source s);
   bool        SourceFromName(const std::string& s, Source* out);

   struct Field
   {
      std::string name;
      std::string label;
      std::string category;
      std::string category_label;
      Source      source       = Source::User;
      int         arity        = 0;          // 0,1,2 date args
      std::string return_type;                // "money", "int", "pct", "text", "date"
      bool        supports_predicate = false; // true for aggregator fields
      std::string description;

      //--- Numeric evaluator (nullptr for text-only identifiers).
      std::function<double(const std::vector<int64_t>& date_args,
                           const Predicate* predicate,
                           const EvalContext& ctx)> num;

      //--- Text evaluator (only for identifiers).
      std::function<std::string(const EvalContext& ctx)> txt;
   };

   //--- Per-source filterable field metadata.
   enum class FilterValueType { Num, Text, Enum };

   struct FilterableField
   {
      std::string                                  name;
      std::string                                  label;
      FilterValueType                              type = FilterValueType::Num;
      std::vector<std::pair<int64_t, std::string>> enum_values;   // for Enum
   };

   const std::vector<FilterableField>& FilterableFor(Source s);

   const std::vector<Field>& All();
   const Field*              Lookup(const std::string& name);

   //--- Throws std::runtime_error on unknown name / missing source / wrong arity.
   //--- `bucket` is the optional bucket key for deposit-bucket fields (resolved
   //--- against ctx.deposit_filter at eval time); empty for all other fields.
   double                    EvaluateNumeric(const std::string& name,
                                             const std::vector<std::string>& args,
                                             const Predicate* predicate,
                                             const std::string& bucket,
                                             const EvalContext& ctx);
   std::string               EvaluateText(const std::string& name,
                                          const EvalContext& ctx);

   //--- Per-source predicate evaluation (used by aggregators).
   bool EvalDealPredicate    (const Predicate& p, const DealRow& d);
   bool EvalDailyPredicate   (const Predicate& p, const DailyRow& d);
   bool EvalPositionPredicate(const Predicate& p, const PositionRow& d);
   bool EvalOrderPredicate   (const Predicate& p, const OrderRow& d);
   bool EvalUserPredicate    (const Predicate& p, const UserInfo& d);

   //--- Walk a Predicate and collect distinct cmp.field names (in first-seen order).
   std::vector<std::string> CollectPredicateFields(const Predicate& p);

   //--- Stringify a User field for display (text → as-is, num → "%g", enum/date → numeric).
   //--- Returns "" if the field name is unknown.
   std::string GetUserFieldString(const UserInfo& u, const std::string& field);

   //--- Validation: returns list of human-readable errors. ok = errors.empty().
   struct ValidationError { std::string path; std::string message; };

   //--- Standalone predicate validation against a source's filterable schema
   //--- (used by AccountFilter routes — no enclosing ReportTemplate).
   std::vector<ValidationError> ValidatePredicateStandalone(const Predicate& p, Source src);

   //--- Source-set analysis for one template (used by Engine to lazy-fetch).
   struct AnalyzeResult
   {
      std::set<Source>     sources;
      std::set<std::string> snapshot_date_params;   // names referenced by Daily start/end
      std::set<std::string> range_date_params;      // names referenced by range aggregations (from + to)
   };
   AnalyzeResult Analyze(const ReportTemplate& tpl);

   std::vector<ValidationError> Validate(const ReportTemplate& tpl);

   //--- JSON dump of the catalog for GET /api/reports/fields.
   nlohmann::json CatalogToJson();
}
