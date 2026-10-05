#include "json_contracts_internal.h"

#include <initializer_list>
#include <string_view>
#include <utility>

namespace physics_arena
{
ArenaStatus ParseReports(const OrderedJson& document, Catalog* catalog, StatusRecord* error)
{
	if (ValidateSchema(document, 2, "config/reports.json", error) != ArenaStatus_Ok ||
	    ValidateKeys(document, {"schema_version", "reports"}, "config/reports.json", error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	OrderedJson::const_iterator reports = document.find("reports");
	if (reports == document.end() || !reports->is_array() || reports->size() > kReportCapacity)
		return SetError(error, "invalid_reports");
	for (const OrderedJson& value : *reports)
	{
		if (!value.is_object())
			return SetError(error, "invalid_report_entry");
		const PresenceStatus chartMetric =
		    value.contains("chart_metric") ? PresenceStatus_Present : PresenceStatus_Absent;
		const PresenceStatus chartLabel =
		    value.contains("chart_label") ? PresenceStatus_Present : PresenceStatus_Absent;
		const PresenceStatus chartNote = value.contains("chart_note") ? PresenceStatus_Present : PresenceStatus_Absent;
		if (chartMetric != chartLabel || chartMetric != chartNote)
			return SetError(error, "incomplete_report_chart_override");
		if (ValidateKeys(value,
		                 chartMetric == PresenceStatus_Present
		                     ? std::initializer_list<std::string_view>{"id", "title", "case_id", "result_source",
							                                           "chart_metric", "chart_label", "chart_note"}
							 : std::initializer_list<std::string_view>{"id", "title", "case_id", "result_source"},
		                 "config/reports.json report", error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		ReportRecord& record = catalog->reports[catalog->reportCount];
		std::string_view id;
		std::string_view text;
		if (RequiredString(value, "id", "report", &id, error) != ArenaStatus_Ok ||
		    StoreText(catalog, &record.id, id) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		for (std::uint32_t prior = 0; prior < catalog->reportCount; ++prior)
			if (CatalogTextView(catalog, catalog->reports[prior].id) == id)
				return SetErrorParts(error, {"duplicate_report_id=", id});
		if (RequiredString(value, "case_id", id, &text, error) != ArenaStatus_Ok ||
		    StoreText(catalog, &record.caseId, text) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		std::uint32_t caseIndex = 0;
		if (CaseIndex(*catalog, text, &caseIndex) != ArenaStatus_Ok)
			return SetErrorParts(error, {"invalid_report_case report=", id});
		for (std::uint32_t prior = 0; prior < catalog->reportCount; ++prior)
			if (CatalogTextView(catalog, catalog->reports[prior].caseId) == text)
				return SetErrorParts(error, {"duplicate_report_case=", text});
		for (std::pair<const char*, CatalogText*> field :
		     {std::pair<const char*, CatalogText*>{"title", &record.title},
		      std::pair<const char*, CatalogText*>{"result_source", &record.resultSource}})
		{
			if (RequiredString(value, field.first, id, &text, error) != ArenaStatus_Ok ||
			    StoreText(catalog, field.second, text) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
		}
		if (SafeRelativePath(CatalogTextView(catalog, record.resultSource)) != ArenaStatus_Ok)
			return SetErrorParts(error, {"invalid_report_path report=", id});
		if (chartMetric == PresenceStatus_Present)
		{
			for (std::pair<const char*, CatalogText*> field :
			     {std::pair<const char*, CatalogText*>{"chart_metric", &record.chartMetric},
			      std::pair<const char*, CatalogText*>{"chart_label", &record.chartLabel},
			      std::pair<const char*, CatalogText*>{"chart_note", &record.chartNote}})
				if (RequiredString(value, field.first, id, &text, error) != ArenaStatus_Ok ||
				    StoreText(catalog, field.second, text) != ArenaStatus_Ok)
					return ArenaStatus_InvalidResult;
			record.chartOverridePresence = PresenceStatus_Present;
		}
		catalog->reportCount += 1;
	}
	return ArenaStatus_Ok;
}

} // namespace physics_arena
