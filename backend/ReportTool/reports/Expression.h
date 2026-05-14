//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     Expression.h - AST type, JSON ser/de, evaluator              |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include "EvalContext.h"
#include "../third_party/json.hpp"
#include <string>
#include <memory>

namespace Expression
{
   //--- Tree-walker evaluator. Throws std::runtime_error on unknown field
   //--- or on a field whose required data source is not present in `ctx`.
   //--- Division by zero clamps to 0.0.
   double Evaluate(const ExprNode& n, const EvalContext& ctx);

   //--- JSON serialization ------------------------------------------

   nlohmann::json NodeToJson(const ExprNode& n);
   bool           NodeFromJson(const nlohmann::json& j,
                               std::shared_ptr<ExprNode>* out,
                               std::string* err);

   nlohmann::json PredicateToJson(const Predicate& p);
   bool           PredicateFromJson(const nlohmann::json& j,
                                    std::shared_ptr<Predicate>* out,
                                    std::string* err);

   //--- Whole-template helpers --------------------------------------

   //--- Convert column list to/from JSON string (stored in report_templates.columns_json)
   std::string  ColumnsToJsonString(const std::vector<ColumnSpec>& cols);
   bool         ColumnsFromJsonString(const std::string& s,
                                      std::vector<ColumnSpec>* out,
                                      std::string* err);

   std::string  SortToJsonString(const SortSpec& s);
   bool         SortFromJsonString(const std::string& s, SortSpec* out);

   std::string  DateParamsToJsonString(const std::vector<std::string>& v);
   std::vector<std::string> DateParamsFromJsonString(const std::string& s);

   //--- Maximum AST nesting depth permitted by validator
   constexpr int kMaxDepth = 20;

   //--- Recursively measure depth (Field/Literal = 1, BinOp = 1 + max(left,right))
   int Depth(const ExprNode& n);

   //--- Format a Predicate tree for textual round-trip.
   std::string FormatPredicateText(const Predicate& p);
}
