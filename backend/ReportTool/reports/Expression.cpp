//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       Expression.cpp             |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "Expression.h"
#include "FieldCatalog.h"

using nlohmann::json;

double Expression::Evaluate(const ExprNode& n, const EvalContext& ctx)
{
   switch(n.type)
   {
      case ExprNode::Type::Literal:
         return n.literal_value;
      case ExprNode::Type::Field:
         return FieldCatalog::EvaluateNumeric(n.field_name, n.field_args, n.predicate.get(), ctx);
      case ExprNode::Type::BinOp:
      {
         const double l = n.left  ? Evaluate(*n.left,  ctx) : 0.0;
         const double r = n.right ? Evaluate(*n.right, ctx) : 0.0;
         switch(n.op)
         {
            case '+': return l + r;
            case '-': return l - r;
            case '*': return l * r;
            case '/': return r == 0.0 ? 0.0 : (l / r);
         }
         return 0.0;
      }
   }
   return 0.0;
}

namespace
{
   const char* KindToStr(ColumnSpec::Kind k)
   {
      return k == ColumnSpec::Kind::Identifier ? "identifier" : "formula";
   }
   ColumnSpec::Kind KindFromStr(const std::string& s)
   {
      return s == "identifier" ? ColumnSpec::Kind::Identifier : ColumnSpec::Kind::Formula;
   }
   const char* FormatToStr(ColumnSpec::Format f)
   {
      switch(f)
      {
         case ColumnSpec::Format::Money: return "money";
         case ColumnSpec::Format::Pct:   return "pct";
         case ColumnSpec::Format::Int:   return "int";
         case ColumnSpec::Format::Text:  return "text";
         case ColumnSpec::Format::Date:  return "date";
      }
      return "money";
   }
   ColumnSpec::Format FormatFromStr(const std::string& s)
   {
      if(s == "pct")  return ColumnSpec::Format::Pct;
      if(s == "int")  return ColumnSpec::Format::Int;
      if(s == "text") return ColumnSpec::Format::Text;
      if(s == "date") return ColumnSpec::Format::Date;
      return ColumnSpec::Format::Money;
   }
}

json Expression::PredicateToJson(const Predicate& p)
{
   json j;
   switch(p.kind)
   {
      case Predicate::Kind::Cmp:
      {
         j["kind"]  = "cmp";
         j["field"] = p.cmp.field;
         j["op"]    = FilterOpName(p.cmp.op);
         if(p.cmp.op == FilterOp::In)
         {
            json arr = json::array();
            if(p.cmp.is_numeric)
               for(double v : p.cmp.value_list_num) arr.push_back(v);
            else
               for(const auto& s : p.cmp.value_list) arr.push_back(s);
            j["value"] = arr;
         }
         else if(p.cmp.is_numeric)
            j["value"] = p.cmp.value_num;
         else
            j["value"] = p.cmp.value_str;
         break;
      }
      case Predicate::Kind::And:
      case Predicate::Kind::Or:
      {
         j["kind"] = p.kind == Predicate::Kind::And ? "and" : "or";
         json items = json::array();
         for(const auto& c : p.children) if(c) items.push_back(PredicateToJson(*c));
         j["items"] = items;
         break;
      }
      case Predicate::Kind::Not:
      {
         j["kind"] = "not";
         j["item"] = p.child ? PredicateToJson(*p.child) : json(nullptr);
         break;
      }
   }
   return j;
}

bool Expression::PredicateFromJson(const json& j,
                                   std::shared_ptr<Predicate>* out,
                                   std::string* err)
{
   if(!j.is_object()) { if(err) *err = "predicate must be an object"; return false; }
   const std::string kind = j.value("kind", "");
   auto node = std::make_shared<Predicate>();

   if(kind == "cmp")
   {
      node->kind = Predicate::Kind::Cmp;
      node->cmp.field = j.value("field", "");
      if(node->cmp.field.empty()) { if(err) *err = "cmp missing 'field'"; return false; }
      const std::string op_s = j.value("op", "");
      if(!FilterOpFromName(op_s, &node->cmp.op))
      { if(err) *err = "cmp has unknown op '" + op_s + "'"; return false; }

      if(!j.contains("value")) { if(err) *err = "cmp missing 'value'"; return false; }
      const auto& v = j["value"];
      if(node->cmp.op == FilterOp::In)
      {
         if(!v.is_array()) { if(err) *err = "'in' value must be an array"; return false; }
         //--- mixed support: if first element numeric → numeric list, else strings
         bool numeric = !v.empty() && v.front().is_number();
         node->cmp.is_numeric = numeric;
         for(const auto& el : v)
         {
            if(numeric && el.is_number())      node->cmp.value_list_num.push_back(el.get<double>());
            else if(!numeric && el.is_string()) node->cmp.value_list.push_back(el.get<std::string>());
            else { if(err) *err = "'in' values must all be numbers or all strings"; return false; }
         }
      }
      else if(v.is_number())
      {
         node->cmp.is_numeric = true;
         node->cmp.value_num = v.get<double>();
      }
      else if(v.is_string())
      {
         node->cmp.is_numeric = false;
         node->cmp.value_str = v.get<std::string>();
      }
      else { if(err) *err = "cmp 'value' must be number, string, or array"; return false; }
   }
   else if(kind == "and" || kind == "or")
   {
      node->kind = (kind == "and") ? Predicate::Kind::And : Predicate::Kind::Or;
      if(!j.contains("items") || !j["items"].is_array())
      { if(err) *err = kind + " missing 'items' array"; return false; }
      for(const auto& it : j["items"])
      {
         std::shared_ptr<Predicate> c;
         if(!PredicateFromJson(it, &c, err)) return false;
         node->children.push_back(c);
      }
   }
   else if(kind == "not")
   {
      node->kind = Predicate::Kind::Not;
      if(!j.contains("item")) { if(err) *err = "not missing 'item'"; return false; }
      if(!PredicateFromJson(j["item"], &node->child, err)) return false;
   }
   else { if(err) *err = "predicate kind must be cmp | and | or | not"; return false; }

   *out = node;
   return true;
}

json Expression::NodeToJson(const ExprNode& n)
{
   json j;
   switch(n.type)
   {
      case ExprNode::Type::Literal:
         j["type"]  = "literal";
         j["value"] = n.literal_value;
         break;
      case ExprNode::Type::Field:
         j["type"] = "field";
         j["name"] = n.field_name;
         j["args"] = n.field_args;
         if(n.predicate) j["predicate"] = PredicateToJson(*n.predicate);
         break;
      case ExprNode::Type::BinOp:
      {
         j["type"]  = "binop";
         j["op"]    = std::string(1, n.op);
         j["left"]  = n.left  ? NodeToJson(*n.left)  : json(nullptr);
         j["right"] = n.right ? NodeToJson(*n.right) : json(nullptr);
         break;
      }
   }
   return j;
}

bool Expression::NodeFromJson(const json& j,
                              std::shared_ptr<ExprNode>* out,
                              std::string* err)
{
   if(!j.is_object())
   {
      if(err) *err = "expr node must be an object";
      return false;
   }
   const std::string t = j.value("type", "");
   auto node = std::make_shared<ExprNode>();
   if(t == "literal")
   {
      node->type = ExprNode::Type::Literal;
      if(j.contains("value") && j["value"].is_number())
         node->literal_value = j["value"].get<double>();
      else { if(err) *err = "literal missing numeric 'value'"; return false; }
   }
   else if(t == "field")
   {
      node->type = ExprNode::Type::Field;
      node->field_name = j.value("name", "");
      if(node->field_name.empty()) { if(err) *err = "field missing 'name'"; return false; }
      if(j.contains("args") && j["args"].is_array())
         for(const auto& a : j["args"])
            if(a.is_string()) node->field_args.push_back(a.get<std::string>());
      if(j.contains("predicate") && !j["predicate"].is_null())
      {
         if(!PredicateFromJson(j["predicate"], &node->predicate, err)) return false;
      }
   }
   else if(t == "binop")
   {
      node->type = ExprNode::Type::BinOp;
      const std::string op = j.value("op", "+");
      if(op.size() != 1 || (op[0] != '+' && op[0] != '-' && op[0] != '*' && op[0] != '/'))
      {
         if(err) *err = "binop 'op' must be one of +-*/";
         return false;
      }
      node->op = op[0];
      if(!j.contains("left")  || !NodeFromJson(j["left"],  &node->left,  err))  return false;
      if(!j.contains("right") || !NodeFromJson(j["right"], &node->right, err)) return false;
   }
   else
   {
      if(err) *err = "expr node 'type' must be literal | field | binop";
      return false;
   }
   *out = node;
   return true;
}

int Expression::Depth(const ExprNode& n)
{
   if(n.type != ExprNode::Type::BinOp) return 1;
   const int dl = n.left  ? Depth(*n.left)  : 0;
   const int dr = n.right ? Depth(*n.right) : 0;
   return 1 + (dl > dr ? dl : dr);
}

//--- Format a predicate for code view (textual round-trip).
namespace
{
   const char* OpToText(FilterOp op)
   {
      switch(op)
      {
         case FilterOp::Eq:         return "=";
         case FilterOp::Neq:        return "!=";
         case FilterOp::Lt:         return "<";
         case FilterOp::Lte:        return "<=";
         case FilterOp::Gt:         return ">";
         case FilterOp::Gte:        return ">=";
         case FilterOp::Regex:      return "~";
         case FilterOp::Contains:   return "contains";
         case FilterOp::StartsWith: return "startswith";
         case FilterOp::EndsWith:   return "endswith";
         case FilterOp::In:         return "in";
      }
      return "?";
   }

   std::string QuoteStr(const std::string& s)
   {
      std::string out; out.reserve(s.size() + 2);
      out += '"';
      for(char c : s) { if(c == '"' || c == '\\') out += '\\'; out += c; }
      out += '"';
      return out;
   }

   std::string FormatValue(const FieldFilter& f)
   {
      if(f.op == FilterOp::In)
      {
         std::string out = "[";
         if(f.is_numeric)
         {
            for(size_t i = 0; i < f.value_list_num.size(); ++i)
            {
               if(i) out += ", ";
               char buf[32];
               snprintf(buf, sizeof(buf), "%g", f.value_list_num[i]);
               out += buf;
            }
         }
         else
         {
            for(size_t i = 0; i < f.value_list.size(); ++i)
            {
               if(i) out += ", ";
               out += QuoteStr(f.value_list[i]);
            }
         }
         out += "]";
         return out;
      }
      if(f.is_numeric)
      {
         char buf[32];
         snprintf(buf, sizeof(buf), "%g", f.value_num);
         return buf;
      }
      return QuoteStr(f.value_str);
   }

   std::string FormatPredicate(const Predicate& p);

   std::string WrapChild(const Predicate& parent, const Predicate& child)
   {
      const std::string s = FormatPredicate(child);
      //--- Add parens when child precedence < parent's (or for NOT readability).
      auto rank = [](Predicate::Kind k) {
         if(k == Predicate::Kind::Cmp) return 3;
         if(k == Predicate::Kind::Not) return 2;
         if(k == Predicate::Kind::And) return 1;
         return 0;  // Or
      };
      if(rank(child.kind) < rank(parent.kind)) return "(" + s + ")";
      return s;
   }

   std::string FormatPredicate(const Predicate& p)
   {
      if(p.kind == Predicate::Kind::Cmp)
         return p.cmp.field + " " + OpToText(p.cmp.op) + " " + FormatValue(p.cmp);
      if(p.kind == Predicate::Kind::Not)
         return std::string("NOT ") + (p.child ? WrapChild(p, *p.child) : "()");
      const char* sep = p.kind == Predicate::Kind::And ? " AND " : " OR ";
      std::string out;
      for(size_t i = 0; i < p.children.size(); ++i)
      {
         if(i) out += sep;
         out += p.children[i] ? WrapChild(p, *p.children[i]) : std::string();
      }
      return out;
   }
}

std::string Expression::FormatPredicateText(const Predicate& p)
{
   return FormatPredicate(p);
}

std::string Expression::ColumnsToJsonString(const std::vector<ColumnSpec>& cols)
{
   json arr = json::array();
   for(const auto& c : cols)
   {
      json j;
      j["key"]    = c.key;
      j["label"]  = c.label;
      j["kind"]   = KindToStr(c.kind);
      j["format"] = FormatToStr(c.format);
      if(c.kind == ColumnSpec::Kind::Identifier)
         j["source"] = c.source;
      else if(c.expr)
         j["expr"] = NodeToJson(*c.expr);
      arr.push_back(std::move(j));
   }
   return arr.dump();
}

bool Expression::ColumnsFromJsonString(const std::string& s,
                                       std::vector<ColumnSpec>* out,
                                       std::string* err)
{
   json arr = json::parse(s, nullptr, false);
   if(arr.is_discarded() || !arr.is_array())
   {
      if(err) *err = "columns_json must be a JSON array";
      return false;
   }
   for(const auto& j : arr)
   {
      ColumnSpec c;
      c.key    = j.value("key", "");
      c.label  = j.value("label", "");
      c.kind   = KindFromStr(j.value("kind", "formula"));
      c.format = FormatFromStr(j.value("format", "money"));
      if(c.kind == ColumnSpec::Kind::Identifier)
      {
         c.source = j.value("source", "");
      }
      else if(j.contains("expr"))
      {
         std::string e;
         if(!NodeFromJson(j["expr"], &c.expr, &e))
         {
            if(err) *err = "column '" + c.key + "': " + e;
            return false;
         }
      }
      out->push_back(std::move(c));
   }
   return true;
}

std::string Expression::SortToJsonString(const SortSpec& s)
{
   return json{ {"column_key", s.column_key}, {"direction", s.descending ? "desc" : "asc"} }.dump();
}

bool Expression::SortFromJsonString(const std::string& s, SortSpec* out)
{
   json j = json::parse(s, nullptr, false);
   if(j.is_discarded() || !j.is_object()) return false;
   out->column_key = j.value("column_key", "");
   out->descending = j.value("direction", std::string("desc")) != "asc";
   return true;
}

std::string Expression::DateParamsToJsonString(const std::vector<std::string>& v)
{
   return json(v).dump();
}

std::vector<std::string> Expression::DateParamsFromJsonString(const std::string& s)
{
   std::vector<std::string> out;
   json j = json::parse(s, nullptr, false);
   if(!j.is_discarded() && j.is_array())
      for(const auto& v : j)
         if(v.is_string()) out.push_back(v.get<std::string>());
   return out;
}
