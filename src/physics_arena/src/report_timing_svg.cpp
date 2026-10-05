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
ArenaStatus WriteStepTimingSvg(const wchar_t* repositoryRoot, const wchar_t* resultDirectory, const Catalog* catalog,
                               const ResultViewModel* model, TimingProjectionScratch* timingScratch,
                               ReportPipelineRecord* record, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || resultDirectory == nullptr || catalog == nullptr || model == nullptr ||
	    timingScratch == nullptr || record == nullptr || error == nullptr ||
	    model->timing.availability != PresenceStatus_Present || model->engineCount == 0 || model->threadCount == 0 ||
	    model->summaryRowCount == 0)
		return error != nullptr ? ReportError(error, ArenaStatus_InvalidArgument, "timing_svg_argument")
		                        : ArenaStatus_InvalidArgument;
	if (PreflightReportInputs(repositoryRoot, catalog, model, error) != ArenaStatus_Ok)
		return error->code;
	ReportOutcomeInput outcomes = {};
	ReportOutcomePresentation outcomePresentation = {};
	if (LoadReportOutcomes(resultDirectory, model, &outcomes, error) != ArenaStatus_Ok ||
	    BuildReportOutcomePresentation(model, outcomes, UINT32_MAX, 0, &outcomePresentation, error) != ArenaStatus_Ok)
		return error->code;
	std::array<LegendRowView, kEngineCapacity> legendRows = {};
	const std::uint32_t legendRowCount = BuildLegendRows(model, &legendRows);
	std::array<std::uint32_t, kEngineCapacity> appearance = {};
	std::uint32_t appearanceCount = 0;
	for (std::uint32_t row = 0; row < legendRowCount; ++row)
		for (std::uint32_t item = 0; item < legendRows[row].count; ++item)
			appearance[appearanceCount++] = legendRows[row].engineOrdinals[item];
	if (appearanceCount != model->engineCount)
		return ReportError(error, ArenaStatus_InvalidResult, "timing_svg_engine_appearance");
	std::array<std::uint32_t, kThreadCountCapacity> threads = {};
	std::copy(model->threadCounts.begin(), model->threadCounts.begin() + model->threadCount, threads.begin());
	std::sort(threads.begin(), threads.begin() + model->threadCount);
	std::array<ReportCell, 3> hostLines = {};
	ReportCell hostCombined = {};
	std::uint32_t hostLineCount = 0;
	if (BuildHostLabel(&model->host, &hostCombined, PresenceStatus_Present, &hostLines, &hostLineCount) !=
	        ArenaStatus_Ok ||
	    hostLineCount == 0)
		return ReportError(error, ArenaStatus_InvalidResult, "timing_svg_host");
	ReportCaseMeta caseMeta = {};
	if (BuildReportCaseMeta(catalog, model, model->summaryRows[0], &caseMeta, error) != ArenaStatus_Ok)
		return error->code;
	ReportCell runLine = {};
	if (BuildReportRunMeta(model, std::span<std::uint32_t>(threads).first(model->threadCount), &runLine) !=
	    ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "timing_svg_run");
	ReportCell metricText = {};
	if (AppendCellText(&metricText, "Median per-") != ArenaStatus_Ok ||
	    AppendCellText(&metricText, ResultViewTextView(model, model->workUnitLabel)) != ArenaStatus_Ok ||
	    AppendCellText(&metricText, " Physics time (ms) across repeats, lower is better") != ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "timing_svg_metric");
	const std::uint32_t hostHeight = std::max(30U, 12 + 18 * hostLineCount);
	const std::uint32_t engineHeight = std::max(30U, 12 + 22 * legendRowCount);
	PhysicsMetaProjection physics = {};
	const std::size_t factCapacity =
	    PhysicsFactCapacity(model, std::span<std::uint32_t>(appearance).first(appearanceCount));
	physics.facts = {static_cast<PhysicsFact*>(_alloca(factCapacity * sizeof(PhysicsFact))), factCapacity};
	if (BuildPhysicsMeta(catalog, model, appearance, appearanceCount, 0, caseMeta, &physics) != ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "report_physics_capacity");
	std::uint32_t physicsRows = 0;
	if (CountPhysicsMetaRows(model, physics, &physicsRows, error) != ArenaStatus_Ok)
		return error->code;
	const std::uint32_t physicsHeight = physicsRows == 0 ? 0 : 12 + 18 * physicsRows;
	const std::uint32_t caseRows = MetaTextRows(CellView(caseMeta.caseLine));
	const std::uint32_t caseHeight = std::max(30U, 12 + 18 * caseRows);
	const std::uint32_t descriptionRows = MetaTextRows(CellView(caseMeta.description));
	const std::uint32_t descriptionHeight = std::max(36U, 12 + 18 * descriptionRows);
	const std::uint32_t buildHeight = std::max(30U, 12 + 18 * appearanceCount);
	const std::uint32_t runRows = MetaTextRows(CellView(runLine));
	const std::uint32_t runHeight = std::max(30U, 12 + 18 * runRows);
	const std::uint32_t metaTotalHeight =
	    30 + caseHeight + descriptionHeight + runHeight + hostHeight + engineHeight + physicsHeight + buildHeight + outcomePresentation.height;
	const std::uint32_t panelCount = model->threadCount;
	const std::uint32_t columns = panelCount <= 1 ? 1 : 2;
	const std::uint32_t rows = (panelCount + columns - 1) / columns;
	const double panelWidth = (1216.0 - 18.0 * (columns - 1)) / columns;
	const double panelHeight = 280.0;
	const double panelGap = 18.0;
	const std::uint32_t chartLegendHeight = std::max(30U, 12 + 22 * legendRowCount);
	const std::uint32_t chartLegendTop = 76 + metaTotalHeight + 12;
	const std::uint32_t groupTop = chartLegendTop + chartLegendHeight + 12;
	const std::uint32_t chartHeight = static_cast<std::uint32_t>(rows * panelHeight + (rows - 1) * panelGap);
	const std::uint32_t height = std::max(720U, groupTop + chartHeight + 36);
	std::array<wchar_t, kReportPathCapacity> temporaryPath = {};
	ReportWriter writer = {};
	if (OpenAtomicReport(resultDirectory, L"step-timing.svg.tmp", &writer, &temporaryPath, error) != ArenaStatus_Ok)
		return error->code;
	ArenaStatus status =
	    WriteFormat(&writer, error,
		            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1280\" height=\"%u\" viewBox=\"0 0 1280 %u\">\n"
		            "<defs>\n<style><![CDATA[\n"
		            "text{font-family:Inter,Segoe UI,Arial,sans-serif;fill:#0f172a;dominant-baseline:middle}\n"
		            ".tab{font-variant-numeric:tabular-nums;font-feature-settings:\"tnum\" 1}\n"
		            ".title{font-size:22px;font-weight:760}.panel-title{font-size:14px;font-weight:700}\n"
		            ".meta-label{font-size:13px;font-weight:760;fill:#0f172a}.meta-value{font-size:13.5px;fill:#26364d}"
		            ".meta-key{font-weight:650;fill:#102033}.axis{font-size:11px;fill:#475569}"
		            ".legend{font-size:13px;font-weight:560}\n"
		            "]]></style>\n",
		            height, height);
	if (status == ArenaStatus_Ok)
		status = WriteEngineLogoDefinitions(repositoryRoot, &writer, model, error);
	if (status == ArenaStatus_Ok)
		status = WriteFormat(
		    &writer, error,
		    "</defs>\n"
		    "<rect width=\"100%%\" height=\"100%%\" fill=\"#f6f8fb\"/>\n"
		    "<rect x=\"24\" y=\"24\" width=\"1232\" height=\"%u\" rx=\"14\" fill=\"#ffffff\" stroke=\"#d9e0ea\"/>\n"
		    "<text x=\"48\" y=\"58\" cl"
		    "ass=\"title\">",
		    height - 48);
	if (status == ArenaStatus_Ok)
		status = WriteXmlText(&writer, ResultViewTextView(model, model->caseDisplayName), error);
	if (status == ArenaStatus_Ok)
		status = WriteReport(&writer, " ", error);
	if (status == ArenaStatus_Ok)
		status = WriteXmlText(&writer, ResultViewTextView(model, model->workUnitLabel), error);
	if (status == ArenaStatus_Ok)
		status = WriteFormat(
		    &writer, error,
		    " timing</text>\n"
		    "<rect x=\"40\" y=\"76\" width=\"1192\" height=\"%u\" rx=\"9\" fill=\"#fbfdff\" stroke=\"#e2e8f0\"/>\n",
		    metaTotalHeight);
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
	if (outcomePresentation.height != 0)
	{
		if (status == ArenaStatus_Ok) status = WriteReportOutcomeSvg(&writer, metaY, model, outcomes,
		    outcomePresentation, 0, error);
		metaY += outcomePresentation.height;
		if (status == ArenaStatus_Ok) status = divider(metaY);
	}
	if (status == ArenaStatus_Ok)
		status = label("Metric", metaY + 15.0);
	if (status == ArenaStatus_Ok)
		status = WritePlainMeta(&writer, 150, metaY + 15.0, CellView(metricText), error);
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
	if (status == ArenaStatus_Ok)
		status = label("Host", metaY + hostHeight / 2.0);
	double lineY = metaY + hostHeight / 2.0 - (hostLineCount - 1) * 9.0;
	for (std::uint32_t line = 0; status == ArenaStatus_Ok && line < hostLineCount; ++line, lineY += 18.0)
		status = WritePlainMeta(&writer, 150, lineY, CellView(hostLines[line]), error);
	metaY += hostHeight;
	if (status == ArenaStatus_Ok)
		status = divider(metaY);
	if (status == ArenaStatus_Ok)
		status = label("Engines", metaY + engineHeight / 2.0);
	double engineRowY = metaY + engineHeight / 2.0 - (legendRowCount - 1) * 11.0;
	for (std::uint32_t row = 0; status == ArenaStatus_Ok && row < legendRowCount; ++row, engineRowY += 22.0)
	{
		double engineX = 150.0;
		for (std::uint32_t item = 0; status == ArenaStatus_Ok && item < legendRows[row].count; ++item)
		{
			const std::uint32_t ordinal = legendRows[row].engineOrdinals[item];
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
	if (status == ArenaStatus_Ok)
		status = WriteReport(&writer, "<g class=\"chart-legend\">\n", error);
	double chartLegendY = chartLegendTop + chartLegendHeight / 2.0 - (legendRowCount - 1) * 11.0;
	for (std::uint32_t row = 0; status == ArenaStatus_Ok && row < legendRowCount; ++row, chartLegendY += 22.0)
	{
		double engineX = 48.0;
		for (std::uint32_t item = 0; status == ArenaStatus_Ok && item < legendRows[row].count; ++item)
		{
			const std::uint32_t ordinal = legendRows[row].engineOrdinals[item];
			const ResultEngineView& engine = model->engines[ordinal];
			status = WriteFormat(
			    &writer, error,
			    "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#%06x\" stroke-width=\"3\"/>\n",
			    engineX, chartLegendY, engineX + 28.0, chartLegendY, engine.colorRgb);
			double logoWidth = 0.0;
			if (status == ArenaStatus_Ok)
				status =
				    WriteEngineLogo(&writer, model, ordinal, engineX + 38.0, chartLegendY, 18.0, &logoWidth, error);
			if (status == ArenaStatus_Ok)
				status = WriteFormat(&writer, error,
				                     "<text x=\"%.1f\" y=\"%.1f\" cl"
				                     "ass=\"legend\">",
				                     engineX + 38.0 + logoWidth + 9.0, chartLegendY);
			if (status == ArenaStatus_Ok)
				status = WriteXmlText(&writer, ResultViewTextView(model, engine.displayName), error);
			if (status == ArenaStatus_Ok)
				status = WriteReport(&writer, "</text>\n", error);
			engineX +=
			    std::max(150.0, 38.0 + logoWidth + ResultViewTextView(model, engine.displayName).size() * 6.9 + 31.0);
		}
	}
	if (status == ArenaStatus_Ok)
		status = WriteReport(&writer, "</g>\n", error);
	for (std::uint32_t thread = 0; status == ArenaStatus_Ok && thread < model->threadCount; ++thread)
	{
		double maximum = 0.0;
		std::uint32_t firstStep = 0;
		std::uint32_t lastStep = 0;
		for (std::uint32_t engineIndex = 0; status == ArenaStatus_Ok && engineIndex < appearanceCount; ++engineIndex)
		{
			const std::uint32_t engineOrdinal = appearance[engineIndex];
			TimingProjection projection = {};
			TimingProjectionSelection selection = {};
			selection.engineIndex = model->engines[engineOrdinal].catalogEngineIndex;
			selection.threadCount = threads[thread];
			selection.mode = TimingProjectionMode_MedianAcrossRepeats;
			if (ProjectTimingSlice(&model->timing, &selection, timingScratch, &projection, error) != ArenaStatus_Ok)
			{
				status = error->code;
				break;
			}
			if (projection.sampleCount == 0)
				continue;
			if (firstStep == 0)
			{
				firstStep = projection.stepIndexes[0];
				lastStep = projection.stepIndexes[projection.sampleCount - 1];
			}
			for (std::uint32_t sample = 0; sample < projection.sampleCount; ++sample)
				maximum = std::max(maximum, projection.physicsStepMilliseconds[sample]);
		}
		if (status != ArenaStatus_Ok)
			break;
		const double axisMaximum = PositiveAxisCeiling(maximum);
		const std::uint32_t column = thread % columns;
		const std::uint32_t row = thread / columns;
		const double panelX = 32.0 + column * (panelWidth + panelGap);
		const double panelY = groupTop + row * (panelHeight + panelGap);
		const double plotX = panelX + 66.0;
		const double plotY = panelY + 42.0;
		const double plotWidth = panelWidth - 90.0;
		const double plotHeight = panelHeight - 100.0;
		status = WriteFormat(
		    &writer, error,
		    "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"8\" fill=\"#ffffff\" stroke=\"#d9e0ea\"/>\n"
		    "<text x=\"%.1f\" y=\"%.1f\" cl"
		    "ass=\"panel-title\">%u %s</text>\n",
		    panelX, panelY, panelWidth, panelHeight, panelX + 18.0, panelY + 25.0, threads[thread],
		    threads[thread] == 1 ? "thread" : "threads");
		for (std::uint32_t tick = 0; status == ArenaStatus_Ok && tick < 5; ++tick)
		{
			const double tickValue = axisMaximum * tick / 4.0;
			const double y = plotY + plotHeight - tick / 4.0 * plotHeight;
			status = WriteFormat(&writer, error,
			                     "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#e8edf4\"/>\n"
			                     "<text x=\"%.1f\" y=\"%.1f\" cl"
			                     "ass=\"axis tab\" text-anchor=\"end\">%.6g</text>\n",
			                     plotX, y, plotX + plotWidth, y, plotX - 8.0, y, tickValue);
		}
		const std::uint32_t stepSpan = lastStep - firstStep;
		const std::uint32_t xTickCount = std::min(5U, stepSpan + 1);
		for (std::uint32_t tick = 0; status == ArenaStatus_Ok && tick < xTickCount; ++tick)
		{
			const std::uint32_t step = xTickCount == 1 ? firstStep : firstStep + stepSpan * tick / (xTickCount - 1);
			const double x =
			    stepSpan == 0 ? plotX : plotX + (step - firstStep) / static_cast<double>(stepSpan) * plotWidth;
			status = WriteFormat(&writer, error,
			                     "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#e8edf4\"/>\n"
			                     "<text x=\"%.1f\" y=\"%.1f\" cl"
			                     "ass=\"axis x-tick tab\" text-anchor=\"middle\">%u</text>\n",
			                     x, plotY, x, plotY + plotHeight, x, plotY + plotHeight + 16.0, step);
		}
		if (status == ArenaStatus_Ok)
			status = WriteFormat(&writer, error,
			                     "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#94a3b8\"/>\n"
			                     "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#94a3b8\"/>\n"
			                     "<text x=\"%.1f\" y=\"%.1f\" cl"
			                     "ass=\"axis\" text-anchor=\"middle\">",
			                     plotX, plotY + plotHeight, plotX + plotWidth, plotY + plotHeight, plotX, plotY, plotX,
			                     plotY + plotHeight, plotX + plotWidth / 2.0, panelY + panelHeight - 13.0);
		if (status == ArenaStatus_Ok)
			status = WriteXmlText(&writer, ResultViewTextView(model, model->workUnitLabel), error);
		if (status == ArenaStatus_Ok)
			status = WriteFormat(
			    &writer, error,
			    "</text>\n"
			    "<text x=\"%.1f\" y=\"%.1f\" cl"
			    "ass=\"axis\" text-anchor=\"middle\" transform=\"rotate(-90 %.1f %.1f)\">Physics time (ms)</text>\n",
			    panelX + 14.0, plotY + plotHeight / 2.0, panelX + 14.0, plotY + plotHeight / 2.0);
		for (std::uint32_t engineIndex = 0; status == ArenaStatus_Ok && engineIndex < appearanceCount; ++engineIndex)
		{
			const std::uint32_t engineOrdinal = appearance[engineIndex];
			TimingProjection projection = {};
			TimingProjectionSelection selection = {};
			selection.engineIndex = model->engines[engineOrdinal].catalogEngineIndex;
			selection.threadCount = threads[thread];
			selection.mode = TimingProjectionMode_MedianAcrossRepeats;
			if (ProjectTimingSlice(&model->timing, &selection, timingScratch, &projection, error) != ArenaStatus_Ok)
			{
				status = error->code;
				break;
			}
			status =
			    WriteFormat(&writer, error, "<polyline fill=\"none\" stroke=\"#%06x\" stroke-width=\"1.75\" points=\"",
				            model->engines[engineOrdinal].colorRgb);
			for (std::uint32_t sample = 0; status == ArenaStatus_Ok && sample < projection.sampleCount; ++sample)
			{
				const double x = stepSpan == 0 ? plotX
				                               : plotX + (projection.stepIndexes[sample] - firstStep) /
				                                             static_cast<double>(stepSpan) * plotWidth;
				const double value = projection.physicsStepMilliseconds[sample];
				const double y = plotY + plotHeight - value / axisMaximum * plotHeight;
				status = WriteFormat(&writer, error, "%s%.2f,%.2f", sample == 0 ? "" : " ", x, y);
			}
			if (status == ArenaStatus_Ok)
				status = WriteReport(&writer, "\"/>\n", error);
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
	status = CommitAtomicReport(resultDirectory, L"step-timing.svg", &writer, temporaryPath, error);
	if (status == ArenaStatus_Ok)
		record->timingSvgBytes = writer.totalBytes;
	return status;
}

} // namespace physics_arena
