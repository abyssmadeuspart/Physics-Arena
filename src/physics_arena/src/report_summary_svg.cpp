#include "report_pipeline_internal.h"

#include "physics_arena/result_pipeline.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace physics_arena
{
enum SummaryMetricDisplay
{
	SummaryMetricDisplay_NativeUnit,
	SummaryMetricDisplay_MillionQueries,
};

ArenaStatus WriteQualitySummarySvgFile(const wchar_t* resultDirectory, const ResultViewModel* model,
                                       ReportPipelineRecord* record, std::uint32_t filterEngineOrdinal,
                                       std::uint32_t filterThreadCount, const wchar_t* temporaryName,
                                       const wchar_t* finalName, StatusRecord* error)
{
	std::uint32_t rowCount = 0;
	for (std::uint32_t index = 0; index < model->displayRowCount; ++index)
	{
		const ResultSummaryViewRow& row = model->summaryRows[model->displayRowIndexes[index]];
		if ((filterEngineOrdinal >= model->engineCount || row.engineOrdinal == filterEngineOrdinal) &&
		    (filterThreadCount == 0 || row.threadCount == filterThreadCount))
			++rowCount;
	}
	ReportWriter writer = {};
	std::array<wchar_t, kReportPathCapacity> temporaryPath = {};
	if (OpenAtomicReport(resultDirectory, temporaryName, &writer, &temporaryPath, error) != ArenaStatus_Ok)
		return error->code;
	if (model->verificationMode == VerificationMode_Off)
	{
		ArenaStatus status = WriteFormat(&writer, error,
		    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1280\" height=\"%u\">"
		    "<rect width=\"100%%\" height=\"100%%\" fill=\"#10151d\"/>"
		    "<style>text{font:16px sans-serif;fill:#e5e9ef}</style>"
		    "<text x=\"28\" y=\"42\">Verification Off: physical quality not checked</text>"
		    "<text x=\"28\" y=\"76\">Physical observations not collected</text>"
		    "<text x=\"28\" y=\"110\">Engine / threads / completed unverified repeats</text>", std::max(240u, 170 + rowCount * 30));
		std::uint32_t outputRow = 0;
		for (std::uint32_t index = 0; status == ArenaStatus_Ok && index < model->displayRowCount; ++index)
		{
			const ResultSummaryViewRow& row = model->summaryRows[model->displayRowIndexes[index]];
			if ((filterEngineOrdinal < model->engineCount && row.engineOrdinal != filterEngineOrdinal) ||
			    (filterThreadCount != 0 && row.threadCount != filterThreadCount))
				continue;
			status = WriteFormat(&writer, error, "<text x=\"28\" y=\"%u\">", 142 + outputRow++ * 30);
			if (status == ArenaStatus_Ok)
				status = WriteXmlText(&writer, ResultViewTextView(model, model->engines[row.engineOrdinal].displayName), error);
			if (status == ArenaStatus_Ok)
				status = WriteFormat(&writer, error, " / %u / %u (%s)</text>", row.threadCount, row.repeatCount,
				    row.outcome == ObservationOutcome_Failed ? "execution failed" : "unverified");
		}
		if (status == ArenaStatus_Ok) status = WriteReport(&writer, "</svg>\n", error);
		if (status != ArenaStatus_Ok)
		{
			CloseHandle(writer.handle);
			DeleteFileW(temporaryPath.data());
			return status;
		}
		status = CommitAtomicReport(resultDirectory, finalName, &writer, temporaryPath, error);
		if (status == ArenaStatus_Ok) record->summarySvgBytes = writer.totalBytes;
		return status;
	}
	ArenaStatus status = WriteFormat(
	    &writer, error,
	    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1280\" height=\"%u\">"
	    "<rect width=\"100%%\" height=\"100%%\" fill=\"#10151d\"/>"
	    "<style>text{font:16px sans-serif;fill:#e5e9ef}.title{font-size:28px;font-weight:600}</style>"
	    "<text x=\"28\" y=\"42\" class=\"title\">Ragdoll physical quality</text>"
	    "<text x=\"28\" y=\"76\">Observations collected after each completed step. Gaps have no pass tolerance. No speed measurement.</text>"
	    "<text x=\"28\" y=\"104\">Values below span repeats. Coverage totals include all repeats. Joint/step identities are in observations.csv.</text>"
	    "<text x=\"28\" y=\"146\">Engine / threads</text>"
	    "<text x=\"300\" y=\"146\">RMS gap range (m)</text>"
	    "<text x=\"535\" y=\"146\">Maximum gap (m)</text>"
	    "<text x=\"740\" y=\"146\">Valid joint samples</text>"
	    "<text x=\"970\" y=\"146\">Invalid / missing bodies</text>",
	    240 + rowCount * 30);
	std::uint32_t outputRow = 0;
	for (std::uint32_t index = 0; status == ArenaStatus_Ok && index < model->displayRowCount; ++index)
	{
		const ResultSummaryViewRow& row = model->summaryRows[model->displayRowIndexes[index]];
		if ((filterEngineOrdinal < model->engineCount && row.engineOrdinal != filterEngineOrdinal) ||
		    (filterThreadCount != 0 && row.threadCount != filterThreadCount))
			continue;
		std::uint32_t thread = 0;
		while (model->threadCounts[thread] != row.threadCount)
			++thread;
		const ObservationAggregate& rms = *ObservationAggregateAt(&model->observations, row.engineOrdinal, thread, 0);
		const ObservationAggregate& maximum =
		    *ObservationAggregateAt(&model->observations, row.engineOrdinal, thread, 1);
		const ObservationAggregate& joints =
		    *ObservationAggregateAt(&model->observations, row.engineOrdinal, thread, 4);
		const ObservationAggregate& invalid =
		    *ObservationAggregateAt(&model->observations, row.engineOrdinal, thread, 6);
		const ObservationAggregate& missing =
		    *ObservationAggregateAt(&model->observations, row.engineOrdinal, thread, 7);
		const unsigned y = 178 + outputRow++ * 30;
		status = WriteFormat(&writer, error, "<text x=\"28\" y=\"%u\">", y);
		if (status == ArenaStatus_Ok)
			status =
			    WriteXmlText(&writer, ResultViewTextView(model, model->engines[row.engineOrdinal].displayName), error);
		if (status == ArenaStatus_Ok)
			status = WriteFormat(&writer, error,
			                     " / %u</text><text x=\"300\" y=\"%u\">%.6g - %.6g</text>"
			                     "<text x=\"535\" y=\"%u\">%.6g</text>"
			                     "<text x=\"740\" y=\"%u\">%llu / %llu</text>"
			                     "<text x=\"970\" y=\"%u\">%llu / %llu (%s)</text>",
			                     row.threadCount, y, rms.minimumActual.float64Value, rms.maximumActual.float64Value, y,
			                     maximum.maximumActual.float64Value, y,
			                     static_cast<unsigned long long>(joints.actualTotal.unsignedValue),
			                     static_cast<unsigned long long>(joints.expectedTotal.unsignedValue), y,
			                     static_cast<unsigned long long>(invalid.actualTotal.unsignedValue),
			                     static_cast<unsigned long long>(missing.actualTotal.unsignedValue),
			                     row.outcome == ObservationOutcome_Failed ? "failed" : "valid");
	}
	if (status == ArenaStatus_Ok)
		status = WriteReport(&writer, "</svg>\n", error);
	if (status != ArenaStatus_Ok)
	{
		CloseHandle(writer.handle);
		DeleteFileW(temporaryPath.data());
		return status;
	}
	status = CommitAtomicReport(resultDirectory, finalName, &writer, temporaryPath, error);
	if (status == ArenaStatus_Ok)
		record->summarySvgBytes = writer.totalBytes;
	return status;
}

ArenaStatus WriteSummarySvgFile(const wchar_t* repositoryRoot, const wchar_t* resultDirectory, const Catalog* catalog,
                                const ResultViewModel* model, ReportPipelineRecord* record,
                                std::uint32_t filterEngineOrdinal, std::uint32_t filterThreadCount,
                                const wchar_t* temporaryName, const wchar_t* finalName, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || resultDirectory == nullptr || catalog == nullptr || model == nullptr ||
	    record == nullptr || error == nullptr || model->summaryRowCount == 0)
		return error != nullptr ? ReportError(error, ArenaStatus_InvalidArgument, "summary_svg_argument")
		                        : ArenaStatus_InvalidArgument;
	if (PreflightReportInputs(repositoryRoot, catalog, model, error) != ArenaStatus_Ok)
		return error->code;
	ReportOutcomeInput outcomes = {};
	if (LoadReportOutcomes(resultDirectory, model, &outcomes, error) != ArenaStatus_Ok)
		return error->code;
	if (model->measurementMode == ResultMeasurementMode_PhysicalQuality)
		return WriteQualitySummarySvgFile(resultDirectory, model, record, filterEngineOrdinal, filterThreadCount,
		                                  temporaryName, finalName, error);
	std::array<std::uint32_t, kEngineCapacity> appearance = {};
	std::uint32_t appearanceCount = 0;
	if (filterEngineOrdinal < model->engineCount)
		appearance[appearanceCount++] = filterEngineOrdinal;
	else
		appearanceCount = FirstAppearanceEngines(model, &appearance);
	std::array<LegendRowView, kEngineCapacity> legendRows = {};
	std::uint32_t legendRowCount = 0;
	if (filterEngineOrdinal < model->engineCount)
	{
		legendRows[0].engineOrdinals[0] = filterEngineOrdinal;
		legendRows[0].count = 1;
		legendRowCount = 1;
	}
	else
		legendRowCount = BuildLegendRows(model, &legendRows);
	std::array<std::uint32_t, kThreadCountCapacity> threads = {};
	std::uint32_t threadCount = 0;
	if (filterThreadCount != 0)
		threads[threadCount++] = filterThreadCount;
	else
	{
		threadCount = model->threadCount;
		std::copy(model->threadCounts.begin(), model->threadCounts.begin() + model->threadCount, threads.begin());
		std::sort(threads.begin(), threads.begin() + model->threadCount);
	}
	const std::uint32_t panelColumns = threadCount <= 1 ? 1 : 2;
	const std::uint32_t panelRows = (threadCount + panelColumns - 1) / panelColumns;
	std::uint32_t maximumGroupRows = 0;
	for (std::uint32_t thread = 0; thread < threadCount; ++thread)
	{
		std::uint32_t count = 0;
		for (std::uint32_t row = 0; row < model->summaryRowCount; ++row)
			if (model->summaryRows[row].threadCount == threads[thread] &&
			    (filterEngineOrdinal >= model->engineCount ||
			     model->summaryRows[row].engineOrdinal == filterEngineOrdinal))
				++count;
		maximumGroupRows = std::max(maximumGroupRows, count);
	}
	const std::uint32_t panelHeight = 68 + 28 * maximumGroupRows;
	const std::uint32_t chartHeight = panelRows * panelHeight + 18 * (panelRows - 1);
	std::array<ReportCell, 3> hostLines = {};
	ReportCell hostCombined = {};
	std::uint32_t hostLineCount = 0;
	if (BuildHostLabel(&model->host, &hostCombined, PresenceStatus_Present, &hostLines, &hostLineCount) !=
	        ArenaStatus_Ok ||
	    hostLineCount == 0)
		return ReportError(error, ArenaStatus_InvalidResult, "summary_svg_host");
	std::uint32_t metadataRow = 0;
	while (metadataRow < model->summaryRowCount &&
	       ((filterEngineOrdinal < model->engineCount &&
	         model->summaryRows[metadataRow].engineOrdinal != filterEngineOrdinal) ||
	        (filterThreadCount != 0 && model->summaryRows[metadataRow].threadCount != filterThreadCount)))
		++metadataRow;
	if (metadataRow == model->summaryRowCount)
		return ReportError(error, ArenaStatus_InvalidResult, "summary_svg_filter_empty");
	ReportCaseMeta caseMeta = {};
	if (BuildReportCaseMeta(catalog, model, model->summaryRows[metadataRow], &caseMeta, error) != ArenaStatus_Ok)
		return error->code;
	PhysicsMetaProjection physics = {};
	const std::size_t factCapacity =
	    PhysicsFactCapacity(model, std::span<std::uint32_t>(appearance).first(appearanceCount));
	physics.facts = {static_cast<PhysicsFact*>(_alloca(factCapacity * sizeof(PhysicsFact))), factCapacity};
	if (BuildPhysicsMeta(catalog, model, appearance, appearanceCount, filterThreadCount, caseMeta, &physics) !=
	    ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "report_physics_capacity");
	std::uint32_t physicsRows = 0;
	if (CountPhysicsMetaRows(model, physics, &physicsRows, error) != ArenaStatus_Ok)
		return error->code;
	const std::uint32_t physicsHeight = physicsRows == 0 ? 0 : 12 + 18 * physicsRows;
	std::uint32_t unknownCount = 0;
	for (const ResultSummaryViewRow& row : std::span(model->summaryRows).first(model->summaryRowCount))
		if ((filterEngineOrdinal >= model->engineCount || row.engineOrdinal == filterEngineOrdinal) &&
		    (filterThreadCount == 0 || row.threadCount == filterThreadCount) &&
		    (model->resultGroupCount != 0 || model->stabilityRequired == PresenceStatus_Present) &&
		    SummaryReportOutcome(model, outcomes, row) == ObservationOutcome_Unknown)
			++unknownCount;
	ReportOutcomePresentation outcomePresentation = {};
	if (BuildReportOutcomePresentation(model, outcomes, filterEngineOrdinal, filterThreadCount,
	    &outcomePresentation, error) != ArenaStatus_Ok) return error->code;
	const std::uint32_t outcomeHeight = outcomePresentation.height;
	const std::uint32_t caseRows = MetaTextRows(CellView(caseMeta.caseLine));
	const std::uint32_t caseHeight = std::max(30U, 12 + 18 * caseRows);
	const std::uint32_t descriptionRows = MetaTextRows(CellView(caseMeta.description));
	const std::uint32_t descriptionHeight = std::max(36U, 12 + 18 * descriptionRows);
	ReportCell runLine = {};
	if (BuildReportRunMeta(model, std::span<std::uint32_t>(threads).first(threadCount), &runLine) != ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "summary_svg_run");
	const std::uint32_t runRows = MetaTextRows(CellView(runLine));
	const std::uint32_t runHeight = std::max(30U, 12 + 18 * runRows);
	const std::uint32_t metaTotalHeight = 30 + descriptionHeight + caseHeight + runHeight +
	                                      std::max(30U, 12 + 18 * hostLineCount) +
	                                      std::max(30U, 12 + 22 * legendRowCount) + outcomeHeight + physicsHeight +
	                                      std::max(30U, 12 + 18 * appearanceCount);
	const std::uint32_t groupTop = 76 + metaTotalHeight + 28;
	const std::uint32_t height = std::max(520U, groupTop + chartHeight + 36);
	const SummaryMetricDisplay metricDisplay = ResultViewTextView(model, model->primaryMetricId) == "queries_per_second"
	                                               ? SummaryMetricDisplay_MillionQueries
	                                               : SummaryMetricDisplay_NativeUnit;
	const bool queryMetric = metricDisplay == SummaryMetricDisplay_MillionQueries;
	const std::string_view displayLabel =
	    queryMetric ? "Million queries/s" : ResultViewTextView(model, model->chartLabel);
	auto formatValue = [model, metricDisplay](double value, ReportCell* cell) -> ArenaStatus
	{
		if (metricDisplay == SummaryMetricDisplay_MillionQueries)
			return value > 0.0 && value < 10000.0 ? SetCell(cell, "<0.01")
			                                      : FormatCell(cell, "%.2f", value / 1000000.0);
		if (FormatCell(cell, "%.2f ", value) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		return AppendCellText(cell, ResultViewTextView(model, model->primaryMetricUnit));
	};
	std::uint32_t maximumValueCharacters = 0;
	for (std::uint32_t row = 0; row < model->summaryRowCount; ++row)
		if ((filterEngineOrdinal >= model->engineCount ||
		     model->summaryRows[row].engineOrdinal == filterEngineOrdinal) &&
		    (filterThreadCount == 0 || model->summaryRows[row].threadCount == filterThreadCount))
		{
			ReportCell formatted = {};
			if (formatValue(model->summaryRows[row].medianPrimaryValue, &formatted) != ArenaStatus_Ok)
				return ReportError(error, ArenaStatus_InvalidResult, "summary_svg_value");
			maximumValueCharacters = std::max(maximumValueCharacters, formatted.size);
		}
	const double valueColumnWidth = maximumValueCharacters * 8.0;
	const double statusGutterWidth = unknownCount != 0 ? 64.0 : 0.0;
	std::array<wchar_t, kReportPathCapacity> temporaryPath = {};
	ReportWriter writer = {};
	if (OpenAtomicReport(resultDirectory, temporaryName, &writer, &temporaryPath, error) != ArenaStatus_Ok)
		return error->code;
	ArenaStatus status =
	    WriteFormat(&writer, error,
		            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1280\" height=\"%u\" viewBox=\"0 0 1280 %u\">\n"
		            "<defs>\n<style><![CDATA[\n"
		            "  text{font-family:Inter,Segoe UI,Arial,sans-serif;fill:#0f172a;dominant-baseline:middle}\n"
		            "  .tab{font-variant-numeric:tabular-nums;font-feature-settings:\"tnum\" 1}\n"
		            "  .title{font-size:22px;font-weight:760}\n"
		            "  .subtitle{font-size:13px;fill:#475569}\n"
		            "  .meta-label{font-size:13px;font-weight:760;fill:#0f172a}\n"
		            "  .meta-value{font-size:13.5px;fill:#26364d}\n"
		            "  .meta-key{font-weight:650;fill:#102033}\n"
		            "  .legend{font-size:13px;font-weight:560}\n"
		            "  .small{font-size:12px;fill:#475569}\n"
		            "  .column{font-size:12px;fill:#334155;font-weight:620}\n"
		            "  .group{font-size:15px;font-weight:760}\n"
		            "  .row{font-size:13px}\n"
		            "  .value{font-size:13px;font-weight:600}\n"
		            "  .fast{}\n"
		            "  .stability-failure{fill:#b91c1c}\n"
		            "  .failure-mark{fill:none;stroke:#b91c1c;stroke-width:1.8;stroke-linecap:round}\n]]></style>\n",
		            height, height);
	if (status == ArenaStatus_Ok)
		status = WriteEngineLogoDefinitions(repositoryRoot, &writer, model, error);
	if (status == ArenaStatus_Ok)
		status = WriteFormat(
		    &writer, error,
		    "</defs>\n<rect width=\"100%%\" height=\"100%%\" fill=\"#f6f8fb\"/>\n"
		    "<rect x=\"24\" y=\"24\" width=\"1232\" height=\"%u\" rx=\"14\" fill=\"#ffffff\" stroke=\"#d9e0ea\"/>\n"
		    "<text x=\"48\" y=\"58\" cl"
		    "ass=\"title\">",
		    height - 48);
	if (status == ArenaStatus_Ok)
		status = WriteXmlText(&writer, ResultViewTextView(model, model->caseDisplayName), error);
	if (status == ArenaStatus_Ok)
		status = WriteFormat(
		    &writer, error,
		    "</text>\n<rect x=\"40\" y=\"76\" width=\"1192\" height=\"%u\" rx=\"9\" fill=\"#fbfdff\" stroke=\"#e2e8f0\"/>\n",
		    metaTotalHeight);
	const std::uint32_t actualHostLines = hostLineCount;
	ReportCell metricLine = {};
	AppendCellText(&metricLine, "Median ");
	AppendCellText(&metricLine, displayLabel);
	AppendCellText(&metricLine, ", ");
	AppendCellText(&metricLine, ResultViewTextView(model, model->chartNote));
	if (threadCount > 1)
		AppendCellText(&metricLine, ". Axes scale per thread");
	std::uint32_t metaY = 76;
	auto label = [&writer, error](const char* name, double center) -> ArenaStatus
	{
		return WriteFormat(&writer, error,
		                   "<text x=\"48\" y=\"%.1f\" cl"
		                   "ass=\"meta-label\">%s</text>\n",
		                   center, name);
	};
	auto divider = [&writer, error](double y) -> ArenaStatus
	{
		return WriteFormat(
		    &writer, error,
		    "<line x1=\"40\" y1=\"%.1f\" x2=\"1232\" y2=\"%.1f\" stroke=\"#edf2f7\" stroke-width=\"1\"/>\n", y, y);
	};
	if (outcomeHeight != 0)
	{
		if (status == ArenaStatus_Ok) status = WriteReportOutcomeSvg(&writer, metaY, model, outcomes,
		    outcomePresentation, filterThreadCount, error);
		metaY += outcomeHeight;
		if (status == ArenaStatus_Ok) status = divider(metaY);
	}
	if (status == ArenaStatus_Ok)
		status = label("Metric", metaY + 15.0);
	if (status == ArenaStatus_Ok)
		status = WritePlainMeta(&writer, 150, metaY + 15.0, CellView(metricLine), error);
	metaY += 30;
	if (status == ArenaStatus_Ok)
		status = divider(metaY);
	if (status == ArenaStatus_Ok)
		status = label("Case", metaY + caseHeight / 2.0);
	if (status == ArenaStatus_Ok)
		status = WriteWrappedMeta(&writer, 150, metaY + caseHeight / 2.0 - (caseRows - 1) * 9.0,
		                          CellView(caseMeta.caseLine), error);
	metaY += caseHeight;
	if (status == ArenaStatus_Ok)
		status = divider(metaY);
	if (status == ArenaStatus_Ok)
		status = label("Description", metaY + descriptionHeight / 2.0);
	if (status == ArenaStatus_Ok)
		status = WriteWrappedMeta(&writer, 150, metaY + descriptionHeight / 2.0 - (descriptionRows - 1) * 9.0,
		                          CellView(caseMeta.description), error);
	metaY += descriptionHeight;
	if (status == ArenaStatus_Ok)
		status = divider(metaY);
	if (status == ArenaStatus_Ok)
		status = label("Run", metaY + runHeight / 2.0);
	if (status == ArenaStatus_Ok)
		status =
		    WriteWrappedMeta(&writer, 150, metaY + runHeight / 2.0 - (runRows - 1) * 9.0, CellView(runLine), error);
	metaY += runHeight;
	if (status == ArenaStatus_Ok)
		status = divider(metaY);
	const std::uint32_t hostHeight = std::max(30U, 12 + 18 * actualHostLines);
	if (status == ArenaStatus_Ok)
		status = label("Host", metaY + hostHeight / 2.0);
	double lineY = metaY + hostHeight / 2.0 - (actualHostLines - 1) * 9.0;
	for (std::uint32_t line = 0; status == ArenaStatus_Ok && line < actualHostLines; ++line, lineY += 18.0)
		status = WritePlainMeta(&writer, 150, lineY, CellView(hostLines[line]), error);
	metaY += hostHeight;
	if (status == ArenaStatus_Ok)
		status = divider(metaY);
	const std::uint32_t engineHeight = std::max(30U, 12 + 22 * legendRowCount);
	if (status == ArenaStatus_Ok)
		status = label("Engines", metaY + engineHeight / 2.0);
	double engineRowY = metaY + engineHeight / 2.0 - (legendRowCount - 1) * 11.0;
	for (std::uint32_t legend = 0; status == ArenaStatus_Ok && legend < legendRowCount; ++legend, engineRowY += 22.0)
	{
		double engineX = 150.0;
		for (std::uint32_t item = 0; status == ArenaStatus_Ok && item < legendRows[legend].count; ++item)
		{
			const std::uint32_t ordinal = legendRows[legend].engineOrdinals[item];
			const std::string_view engineLabel = ResultViewTextView(model, model->engines[ordinal].provenanceLabel);
			double logoWidth = 0.0;
			status = WriteEngineLogo(&writer, model, ordinal, engineX, engineRowY, 18.0, &logoWidth, error);
			if (status == ArenaStatus_Ok)
				status = WriteFormat(&writer, error,
				                     "<text x=\"%.1f\" y=\"%.1f\" cl"
				                     "ass=\"meta-value\">",
				                     engineX + logoWidth + 9.0, engineRowY);
			if (status == ArenaStatus_Ok)
				status = WriteXmlText(&writer, engineLabel, error);
			if (status == ArenaStatus_Ok)
				status = WriteReport(&writer, "</text>\n", error);
			engineX += std::max(112.0, logoWidth + engineLabel.size() * 6.9 + 31.0);
		}
	}
	metaY += engineHeight;
	if (status == ArenaStatus_Ok)
		status = divider(metaY);
	if (physicsRows != 0)
	{
		if (status == ArenaStatus_Ok)
			status = label("Physics", metaY + 15.0);
		lineY = metaY + physicsHeight / 2.0 - (physicsRows - 1) * 9.0;
		if (status == ArenaStatus_Ok)
			status = WritePhysicsMeta(&writer, lineY, model, physics, error);
		metaY += physicsHeight;
		if (status == ArenaStatus_Ok)
			status = divider(metaY);
	}
	const std::uint32_t buildHeight = std::max(30U, 12 + 18 * appearanceCount);
	if (status == ArenaStatus_Ok)
		status = label("Builds", metaY + buildHeight / 2.0);
	lineY = metaY + buildHeight / 2.0 - (appearanceCount - 1) * 9.0;
	for (std::uint32_t index = 0; status == ArenaStatus_Ok && index < appearanceCount; ++index, lineY += 18.0)
		status = WriteBuildMeta(&writer, 150, lineY, model, appearance[index], error);
	metaY += buildHeight;
	if (status == ArenaStatus_Ok)
		status = WriteFormat(
		    &writer, error, "<line x1=\"126\" y1=\"80\" x2=\"126\" y2=\"%u\" stroke=\"#d9e2ee\" stroke-width=\"1\"/>\n",
		    76 + metaTotalHeight - 5);
	const double panelWidth = (1216.0 - 18.0 * (panelColumns - 1)) / panelColumns;
	for (std::uint32_t group = 0; status == ArenaStatus_Ok && group < threadCount; ++group)
	{
		const std::uint32_t panelColumn = group % panelColumns;
		const std::uint32_t panelRow = group / panelColumns;
		const double panelX = 32.0 + panelColumn * (panelWidth + 18.0);
		const double panelY = groupTop + panelRow * (panelHeight + 18.0);
		const double rowX = panelX + (panelColumns == 1 ? 32.0 : 24.0);
		const double engineTextX = panelX + (panelColumns == 1 ? 74.0 : 66.0);
		const double plotLeft = panelX + (panelColumns == 1 ? 298.0 : 238.0) + statusGutterWidth;
		const double valueX = panelX + (panelColumns == 1 ? 1156.0 : panelWidth - 24.0);
		const double plotWidth = valueX - valueColumnWidth - 16.0 - plotLeft;
		std::array<std::uint32_t, kEngineCapacity> groupRows = {};
		std::uint32_t groupRowCount = 0;
		double rawMaximum = 0.0;
		for (const PresenceStatus failurePartition : {PresenceStatus_Absent, PresenceStatus_Present})
			for (std::uint32_t index = 0; index < model->displayRowCount; ++index)
			{
				const std::uint32_t rowIndex = model->displayRowIndexes[index];
				const ResultSummaryViewRow& row = model->summaryRows[rowIndex];
				const PresenceStatus failed = SummaryReportOutcome(model, outcomes, row) == ObservationOutcome_Failed
				                                  ? PresenceStatus_Present : PresenceStatus_Absent;
				if (failed == failurePartition && row.threadCount == threads[group] &&
				    (filterEngineOrdinal >= model->engineCount || row.engineOrdinal == filterEngineOrdinal))
				{
					groupRows[groupRowCount++] = rowIndex;
					if (row.repeatCount != 0)
						rawMaximum = std::max(rawMaximum, row.medianPrimaryValue);
				}
			}
		const double axisMaximum = PositiveAxisCeiling(rawMaximum);
		const double displayedTickStep = axisMaximum / 5000000.0;
		const int tickPrecision =
		    queryMetric && displayedTickStep < 1.0 ? static_cast<int>(std::ceil(-std::log10(displayedTickStep))) + 1 : 0;
		status = WriteFormat(
		    &writer, error,
		    "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%u\" rx=\"10\" fill=\"%s\" stroke=\"#e2e8f0\"/>\n"
		    "<text x=\"%.1f\" y=\"%.1f\" cl"
		    "ass=\"group\">%u %s</text>\n",
		    panelX, panelY, panelWidth, panelHeight, group % 2 == 0 ? "#fbfdff" : "#ffffff", rowX, panelY + 24.0,
		    threads[group], threads[group] == 1 ? "thread" : "threads");
		for (std::uint32_t tick = 0; status == ArenaStatus_Ok && tick < 6; ++tick)
		{
			const double tickValue = axisMaximum * tick / 5.0;
			const double tickX = plotLeft + tickValue / axisMaximum * plotWidth;
			ReportCell tickText = {};
			if (queryMetric)
				FormatCell(&tickText, "%.*f", tickPrecision, tickValue / 1000000.0);
			else
				FormatCell(&tickText, "%.6g", tickValue);
			status = WriteFormat(&writer, error,
			                     "<text x=\"%.1f\" y=\"%.1f\" cl"
			                     "ass=\"small tab\" text-anchor=\"middle\">%s</text>\n",
			                     tickX, panelY + 24.0, tickText.data.data());
		}
		if (status == ArenaStatus_Ok)
			status = WriteFormat(&writer, error,
			                     "<text x=\"%.1f\" y=\"%.1f\" cl"
			                     "ass=\"small\" text-anchor=\"end\">",
			                     valueX, panelY + 44.0);
		if (status == ArenaStatus_Ok)
			status = WriteXmlText(&writer, displayLabel, error);
		if (status == ArenaStatus_Ok)
			status = WriteFormat(
			    &writer, error,
			    "</text>\n"
			    "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#cfd8e3\" stroke-width=\"1\"/>\n",
			    plotLeft, panelY + 58.0, plotLeft + plotWidth, panelY + 58.0);
		for (std::uint32_t tick = 0; status == ArenaStatus_Ok && tick < 6; ++tick)
		{
			const double tickX = plotLeft + tick / 5.0 * plotWidth;
			status = WriteFormat(
			    &writer, error,
			    "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#cfd8e3\" stroke-width=\"1\"/>\n"
			    "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#e8edf4\" stroke-width=\"1\"/>\n",
			    tickX, panelY + 52.0, tickX, panelY + 64.0, tickX, panelY + 58.0, tickX, panelY + panelHeight - 14.0);
		}
		std::uint32_t fastestRow = 0;
		if (model->stabilityRequired == PresenceStatus_Present)
		{
			fastestRow = groupRowCount;
			for (std::uint32_t candidate = 0; candidate < groupRowCount; ++candidate)
				if (model->summaryRows[groupRows[candidate]].repeatCount != 0 &&
				    SummaryReportOutcome(model, outcomes, model->summaryRows[groupRows[candidate]]) == ObservationOutcome_Ok)
				{
					fastestRow = candidate;
					break;
				}
		}
		double rowY = panelY + 78.0;
		for (std::uint32_t row = 0; status == ArenaStatus_Ok && row < groupRowCount; ++row, rowY += 28.0)
		{
			const ResultSummaryViewRow& value = model->summaryRows[groupRows[row]];
			const ResultEngineView& engine = model->engines[value.engineOrdinal];
			const double barWidth = value.medianPrimaryValue / axisMaximum * plotWidth;
			const ObservationOutcome outcome = SummaryReportOutcome(model, outcomes, value);
			const PresenceStatus fastest = model->verificationMode == VerificationMode_On && row == fastestRow && groupRowCount > 1 && value.repeatCount != 0 && outcome != ObservationOutcome_Failed
			                                   ? PresenceStatus_Present : PresenceStatus_Absent;
			if (outcome == ObservationOutcome_Failed)
				status = WriteFormat(&writer, error,
				                     "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"26\" rx=\"6\" fill=\"#fef2f2\" stroke=\"#fecaca\" stroke-width=\"1\"/>\n",
				                     panelX + 16.0, rowY - 13.0, panelWidth - 32.0);
			if (fastest == PresenceStatus_Present)
				status = WriteFormat(
				    &writer, error,
				    "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"26\" rx=\"6\" fill=\"#fff7ed\" stroke=\"#fed7aa\" stroke-width=\"1\"/>\n",
				    panelX + 16.0, rowY - 13.0, panelWidth - 32.0);
			double logoWidth = 0.0;
			if (status == ArenaStatus_Ok)
				status = WriteEngineLogo(&writer, model, value.engineOrdinal, rowX, rowY, 20.0, &logoWidth, error);
			if (status == ArenaStatus_Ok)
				status = WriteFormat(&writer, error,
				                     "<text x=\"%.1f\" y=\"%.1f\" cl"
				                     "ass=\"%s\">",
				                     engineTextX, rowY, outcome == ObservationOutcome_Failed ? "row stability-failure" :
			                     (fastest == PresenceStatus_Present ? "row fast" : "row"));
			if (status == ArenaStatus_Ok)
				status = WriteXmlText(&writer, ResultViewTextView(model, engine.displayName), error);
			if (status == ArenaStatus_Ok)
				status = WriteReport(&writer, "</text>\n", error);
			if (status == ArenaStatus_Ok && outcome == ObservationOutcome_Failed)
				status = WriteFormat(&writer, error,
				                     "<path d=\"M %.1f %.1f L %.1f %.1f M %.1f %.1f L %.1f %.1f\" class=\"failure-mark\"/>\n",
				                     plotLeft - 24.0, rowY - 4.0, plotLeft - 16.0, rowY + 4.0,
				                     plotLeft - 16.0, rowY - 4.0, plotLeft - 24.0, rowY + 4.0);
			if (status == ArenaStatus_Ok && (model->resultGroupCount != 0 || model->stabilityRequired == PresenceStatus_Present) && outcome == ObservationOutcome_Unknown)
				status = WriteFormat(&writer, error,
				                     "<text x=\"%.1f\" y=\"%.1f\" class=\"small\">%s</text>\n",
				                     plotLeft - statusGutterWidth, rowY, model->stabilityRequired == PresenceStatus_Present ? "Unassessed" : "Unknown");
			if (value.repeatCount == 0)
			{
				if (status == ArenaStatus_Ok)
					status = WriteFormat(&writer, error, "<text x=\"%.1f\" y=\"%.1f\" class=\"small\" text-anchor=\"end\">Unavailable</text>\n", valueX, rowY);
				continue;
			}
			ReportCell formatted = {};
			if (status == ArenaStatus_Ok)
				status = formatValue(value.medianPrimaryValue, &formatted);
			if (status == ArenaStatus_Ok)
				status =
				    WriteFormat(&writer, error,
					            "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"14\" rx=\"4\" fill=\"%s\"/>\n"
					            "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"14\" rx=\"4\" fill=\"#%06x\"/>\n"
					            "<text x=\"%.1f\" y=\"%.1f\" cl"
					            "ass=\"%s\" text-anchor=\"end\">",
					            plotLeft, rowY - 7.0, plotWidth, outcome == ObservationOutcome_Failed ? "#fee2e2" : "#edf2f7",
					            plotLeft, rowY - 7.0, barWidth, outcome == ObservationOutcome_Failed ? 0xdc2626U : engine.colorRgb,
					            valueX, rowY, outcome == ObservationOutcome_Failed ? "value tab stability-failure" :
					            (fastest == PresenceStatus_Present ? "value tab fast" : "value tab"));
			if (status == ArenaStatus_Ok)
				status = WriteXmlText(&writer, CellView(formatted), error);
			if (status == ArenaStatus_Ok)
				status = WriteReport(&writer, "</text>\n", error);
		}
	}
	if (status == ArenaStatus_Ok)
		status = WriteReport(&writer, "</svg>\n", error);
	if (status != ArenaStatus_Ok)
	{
		CloseHandle(writer.handle);
		DeleteFileW(temporaryPath.data());
		return status;
	}
	status = CommitAtomicReport(resultDirectory, finalName, &writer, temporaryPath, error);
	if (status == ArenaStatus_Ok)
		record->summarySvgBytes = writer.totalBytes;
	return status;
}

ArenaStatus WriteSummarySvg(const wchar_t* repositoryRoot, const wchar_t* resultDirectory, const Catalog* catalog,
                            const ResultViewModel* model, ReportPipelineRecord* record, StatusRecord* error)
{
	return WriteSummarySvgFile(repositoryRoot, resultDirectory, catalog, model, record, UINT32_MAX, 0,
	                           L"summary.svg.tmp", L"summary.svg", error);
}

} // namespace physics_arena
