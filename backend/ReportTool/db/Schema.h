#pragma once
#include "SqliteDb.h"

namespace Schema
{
   //--- runs idempotent CREATE TABLE/INDEX. Returns false on first failure.
   bool Apply(SqliteDb& db, std::string* err);
}
