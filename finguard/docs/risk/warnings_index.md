---
title: Warning 编号对照表
tags: [risk, warning, rules]
last_updated: 2026-02-04
source: session 2026-02-04
status: draft
---
# Warning 编号对照表

本表用于统一 warning 的编号、原因模板与规则触发条件。

rules:
  - code: STRUCTURE.MISSING_SECTION
    reason_template: "结构缺失（001）缺少章节：{section}"
    section: "{section}"
    basis_template: "阈值=必须包含章节；来源=模板要求"
    trigger_terms_rule: "结构检查，无触发词"
  - code: CONTENT.MISSING_METRIC
    reason_template: "内容缺失（001）缺少关键财务指标：{metric}"
    section: "财务状况（定量指标，含好价格判断）"
    basis_template: "阈值=必须包含指标；来源=模板要求"
    trigger_terms_rule: "指标缺失检查，无触发词"
  - code: CONTENT.MISSING_SOURCE
    reason_template: "内容缺失（002）缺少引用来源"
    section: "合规与数据声明"
    basis_template: "阈值=必须提供引用来源；来源=业务规则"
    trigger_terms_rule: "引用来源缺失检查，无触发词"
  - code: CONTENT.MISSING_COMPANY_FIELD
    reason_template: "内容缺失（003）公司信息缺少字段：{field}"
    section: "公司信息"
    basis_template: "阈值=必须包含公司信息字段；来源=模板要求"
    trigger_terms_rule: "公司信息字段缺失检查，无触发词"
  - code: CONTENT.MISSING_RETURN_INPUT
    reason_template: "内容缺失（004）预期收益缺少数值代入"
    section: "预期收益（模型估算，必须注明不确定性）"
    basis_template: "阈值=必须包含数值代入；来源=模板要求"
    trigger_terms_rule: "预期收益缺少数值代入检查，无触发词"
  - code: CONTENT.MISSING_CONCLUSION_POINTS
    reason_template: "内容缺失（005）结论要点数量不符合 1-3 条"
    section: "结论（分析性总结）"
    basis_template: "阈值=1-3 条要点；来源=模板要求"
    trigger_terms_rule: "结论要点数量检查，无触发词"
  - code: NUMERIC.THRESHOLD_BREACH
    reason_template: "数值超标（001）指标超出阈值：{metric}"
    section: "财务状况（定量指标，含好价格判断）"
    basis_template: "阈值={threshold}；来源=守拙价值多元化基金理念"
    trigger_terms_rule: "出现明确数值时触发（PEG/负债率/ROE）"
  - code: COMPLIANCE.PROHIBITED
    reason_template: "合规禁止（001）出现禁止内容：{term}"
    section: "{section}"
    basis_template: "阈值=禁止项；来源=业务规则"
    trigger_terms_rule: "命中买卖指令/资产配置比例/收益承诺等禁用词"
  - code: SEMANTIC.NO_BASIS
    reason_template: "语义无依据（001）语义判断缺少阈值与来源"
    section: "财务状况（定量指标，含好价格判断）"
    basis_template: "阈值=需给出阈值与来源；来源=业务规则"
    trigger_terms_rule: "出现“偏高/偏低/过高/过低”等语义判断但未给出依据"
