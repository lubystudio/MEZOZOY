#pragma once

#include "Models.h"

#include <string>
#include <vector>

namespace mezozoy {

std::wstring ProjectDocumentTypeName(ProjectDocumentType type);
std::wstring ProjectDocumentTypeKey(ProjectDocumentType type);
ProjectDocumentType ParseProjectDocumentType(const std::wstring& value);
const std::vector<ProjectDocumentType>& AllProjectDocumentTypes();

std::wstring BreakdownCategoryName(BreakdownCategory category);
std::wstring BreakdownCategoryKey(BreakdownCategory category);
BreakdownCategory ParseBreakdownCategory(const std::wstring& value);
const std::vector<BreakdownCategory>& AllBreakdownCategories();

std::wstring ReviewMarkTypeName(ReviewMarkType type);
std::wstring ReviewMarkTypeKey(ReviewMarkType type);
ReviewMarkType ParseReviewMarkType(const std::wstring& value);
const std::vector<ReviewMarkType>& AllReviewMarkTypes();

}  // namespace mezozoy
