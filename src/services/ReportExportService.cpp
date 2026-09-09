#include "ReportExportService.h"

#include "ScriptService.h"
#include "StatisticsService.h"
#include "../core/Utf.h"

#include <iomanip>
#include <sstream>

namespace mezozoy {
namespace {

std::wstring Csv(std::wstring value) {
    std::wstring escaped; escaped.reserve(value.size() + 2);
    for (const wchar_t c : value) { if (c == L'"') escaped += L"\"\""; else if (c == L'\r' || c == L'\n') escaped += L' '; else escaped += c; }
    return L"\"" + escaped + L"\"";
}

std::wstring Html(std::wstring value) {
    std::wstring result;
    for (const wchar_t c : value) {
        if (c == L'&') result += L"&amp;"; else if (c == L'<') result += L"&lt;"; else if (c == L'>') result += L"&gt;";
        else if (c == L'"') result += L"&quot;"; else result += c;
    }
    return result;
}

}  // namespace

bool ReportExportService::exportSceneCsv(const std::filesystem::path& path, const Project& project, std::wstring* error) const {
    std::wostringstream output; output << L"№;Название;Слов;Минут;Разбор;Комментарии;Статус\r\n";
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        const auto& scene = project.scenes[index]; output << index + 1 << L';' << Csv(scene.title) << L';' << ScriptService::countWords(scene.text) << L';'
            << std::fixed << std::setprecision(2) << ScriptService::estimatedMinutes(scene.text, scene.screenplayFormats) << L';' << scene.breakdownItems.size() << L';'
            << scene.reviewComments.size() << L';' << Csv(scene.status) << L"\r\n";
    }
    const std::string bytes = std::string("\xEF\xBB\xBF") + utf::ToUtf8(output.str()); return utf::WriteFileAtomic(path, bytes, error);
}

bool ReportExportService::exportProjectHtml(const std::filesystem::path& path, const Project& project, std::wstring* error) const {
    StatisticsService statistics; const auto data = statistics.analyze(project); std::wostringstream output;
    output << L"<!doctype html><html lang=\"ru\"><meta charset=\"utf-8\"><title>" << Html(project.title) << L" — отчет Mezozoy</title>"
        L"<style>body{font-family:Segoe UI,Arial;background:#17141f;color:#eee;margin:40px}h1,h2{color:#b880ff}.cards{display:flex;gap:12px;flex-wrap:wrap}.card{background:#292334;padding:16px;border:1px solid #54456b;min-width:150px}table{border-collapse:collapse;width:100%;margin-top:18px}th,td{border:1px solid #54456b;padding:8px;text-align:left}th{background:#352a45}</style>";
    output << L"<h1>" << Html(project.title) << L"</h1><div class=\"cards\"><div class=\"card\">Сцен<br><b>" << project.scenes.size()
        << L"</b></div><div class=\"card\">Слов<br><b>" << data.totalWords << L"</b></div><div class=\"card\">Страниц<br><b>" << std::fixed << std::setprecision(1)
        << data.pages << L"</b></div><div class=\"card\">Минут<br><b>" << data.minutes << L"</b></div></div>";
    output << L"<h2>Сцены</h2><table><tr><th>№</th><th>Название</th><th>Слов</th><th>Минут</th><th>Разбор</th><th>Комментарии</th></tr>";
    for (std::size_t index = 0; index < project.scenes.size(); ++index) { const auto& scene = project.scenes[index]; output << L"<tr><td>" << index + 1 << L"</td><td>" << Html(scene.title) << L"</td><td>" << ScriptService::countWords(scene.text) << L"</td><td>" << std::fixed << std::setprecision(1) << ScriptService::estimatedMinutes(scene.text, scene.screenplayFormats) << L"</td><td>" << scene.breakdownItems.size() << L"</td><td>" << scene.reviewComments.size() << L"</td></tr>"; }
    output << L"</table></html>"; return utf::WriteFileAtomic(path, utf::ToUtf8(output.str()), error);
}

}  // namespace mezozoy
