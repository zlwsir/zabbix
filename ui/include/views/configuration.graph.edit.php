<?php
/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/


/**
 * @var CView $this
 */

$widget = new CWidget();

if ($data['parent_discoveryid'] === null) {
	$widget
		->setTitle(_('Graphs'))
		->setNavigation(getHostNavigation('graphs', $data['hostid']));
}
else {
	$widget
		->setTitle(_('Graph prototypes'))
		->setNavigation(getHostNavigation('graphs', $data['hostid'], $data['parent_discoveryid']));
}

$url = (new CUrl('graphs.php'))
	->setArgument('context', $data['context'])
	->getUrl();

// Create form.
$graphForm = (new CForm('post', $url))
	->setName('graphForm')
	->setAttribute('aria-labeledby', ZBX_STYLE_PAGE_TITLE)
	->addVar('form', $this->data['form'])
	->addVar('hostid', $this->data['hostid'])
	->addVar('ymin_itemid', $data['ymin_itemid'])
	->addVar('ymax_itemid', $data['ymax_itemid']);

if ($data['parent_discoveryid'] !== null) {
	$graphForm->addItem((new CVar('parent_discoveryid', $data['parent_discoveryid']))->removeId());
}

if ($data['graphid'] != 0) {
	$graphForm->addVar('graphid', $data['graphid']);
}

// Create form list.
$graphFormList = new CFormList('graphFormList');

$is_templated = (bool) $this->data['templates'];
if ($is_templated) {
	$graphFormList->addRow(_('Parent graphs'), $data['templates']);
}

$discovered_graph = false;
if (array_key_exists('flags', $data) && $data['flags'] == ZBX_FLAG_DISCOVERY_CREATED) {
	$discovered_graph = true;
}

$readonly = false;
if ($is_templated || $discovered_graph) {
	$readonly = true;
}

if ($discovered_graph) {
	$graphFormList->addRow(_('Discovered by'), new CLink($data['discoveryRule']['name'],
		(new CUrl('graphs.php'))
			->setArgument('form', 'update')
			->setArgument('parent_discoveryid', $data['discoveryRule']['itemid'])
			->setArgument('graphid', $data['graphDiscovery']['parent_graphid'])
			->setArgument('context', $data['context'])
	));
}

$graphFormList
	->addRow(
		(new CLabel(_('Name'), 'name'))->setAsteriskMark(),
		(new CTextBox('name', $this->data['name'], $readonly))
			->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired()
			->setAttribute('autofocus', 'autofocus')
	)
	->addRow((new CLabel(_('Width'), 'width'))->setAsteriskMark(),
		(new CNumericBox('width', $this->data['width'], 5, $readonly))
			->setWidth(ZBX_TEXTAREA_NUMERIC_STANDARD_WIDTH)
			->setAriaRequired()
	)
	->addRow((new CLabel(_('Height'), 'height'))->setAsteriskMark(),
		(new CNumericBox('height', $this->data['height'], 5, $readonly))
			->setWidth(ZBX_TEXTAREA_NUMERIC_STANDARD_WIDTH)
			->setAriaRequired()
	)
	->addRow((new CLabel(_('Graph type'), 'label-graphtype')),
		(new CSelect('graphtype'))
			->setId('graphtype')
			->setFocusableElementId('label-graphtype')
			->setValue($this->data['graphtype'])
			->addOptions(CSelect::createOptionsFromArray(graphType()))
			->setDisabled($readonly)
	)
	->addRow(_('Show legend'),
		(new CCheckBox('show_legend'))
			->setChecked($this->data['show_legend'] == 1)
			->setEnabled(!$readonly)
	);

// Append graph types to form list.
if ($this->data['graphtype'] == GRAPH_TYPE_NORMAL || $this->data['graphtype'] == GRAPH_TYPE_STACKED) {
	$graphFormList->addRow(_('Show working time'),
		(new CCheckBox('show_work_period'))
			->setChecked($this->data['show_work_period'] == 1)
			->setEnabled(!$readonly)
	);
	$graphFormList->addRow(_('Show triggers'),
		(new CCheckbox('show_triggers'))
			->setchecked($this->data['show_triggers'] == 1)
			->setEnabled(!$readonly)
	);

	if ($this->data['graphtype'] == GRAPH_TYPE_NORMAL) {
		// Percent left.
		$percentLeftTextBox = (new CTextBox('percent_left', $this->data['percent_left'], $readonly, 7))
			->setWidth(ZBX_TEXTAREA_TINY_WIDTH);
		$percentLeftCheckbox = (new CCheckBox('visible[percent_left]'))
			->setChecked(true)
			->onClick('javascript: showHideVisible("percent_left");')
			->setEnabled(!$readonly);

		if(isset($this->data['visible']) && isset($this->data['visible']['percent_left'])) {
			$percentLeftCheckbox->setChecked(true);
		}
		elseif ($this->data['percent_left'] == 0) {
			$percentLeftTextBox->addStyle('visibility: hidden;');
			$percentLeftCheckbox->setChecked(false);
		}

		$graphFormList->addRow(_('Percentile line (left)'), [$percentLeftCheckbox, SPACE, $percentLeftTextBox]);

		// Percent right.
		$percentRightTextBox = (new CTextBox('percent_right', $this->data['percent_right'], $readonly, 7))
			->setWidth(ZBX_TEXTAREA_TINY_WIDTH);
		$percentRightCheckbox = (new CCheckBox('visible[percent_right]'))
			->setChecked(true)
			->onClick('javascript: showHideVisible("percent_right");')
			->setEnabled(!$readonly);

		if(isset($this->data['visible']) && isset($this->data['visible']['percent_right'])) {
			$percentRightCheckbox->setChecked(true);
		}
		elseif ($this->data['percent_right'] == 0) {
			$percentRightTextBox->addStyle('visibility: hidden;');
			$percentRightCheckbox->setChecked(false);
		}

		$graphFormList->addRow(_('Percentile line (right)'), [$percentRightCheckbox, SPACE, $percentRightTextBox]);
	}

	$yaxisMinData = [];
	$yaxisMinData[] = (new CSelect('ymin_type'))
		->setId('ymin_type')
		->setValue($this->data['ymin_type'])
		->addOptions(CSelect::createOptionsFromArray([
			GRAPH_YAXIS_TYPE_CALCULATED => _('Calculated'),
			GRAPH_YAXIS_TYPE_FIXED => _('Fixed'),
			GRAPH_YAXIS_TYPE_ITEM_VALUE => _('Item')
		]))
		->setDisabled($readonly);

	if ($this->data['ymin_type'] == GRAPH_YAXIS_TYPE_FIXED) {
		$yaxisMinData[] = (new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN);
		$yaxisMinData[] = (new CTextBox('yaxismin', $this->data['yaxismin'], $readonly))
			->setWidth(ZBX_TEXTAREA_SMALL_WIDTH);
	}
	elseif ($this->data['ymin_type'] == GRAPH_YAXIS_TYPE_ITEM_VALUE) {
		$graphForm->addVar('yaxismin', $this->data['yaxismin']);
		$yaxisMinData[] = (new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN);
		$yaxisMinData[] = (new CTextBox('ymin_name', $data['ymin_item_name'], true))
			->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired();
		$yaxisMinData[] = (new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN);

		// Select item button.
		$yaxisMinData[] = (new CButton('yaxis_min', _('Select')))
			->addClass(ZBX_STYLE_BTN_GREY)
			->onClick(
				'return PopUp("popup.generic", jQuery.extend('.json_encode([
					'srctbl' => 'items',
					'srcfld1' => 'itemid',
					'srcfld2' => 'name',
					'dstfrm' => $graphForm->getName(),
					'dstfld1' => 'ymin_itemid',
					'dstfld2' => 'ymin_name',
					'with_webitems' => '1',
					'numeric' => '1',
					'writeonly' => '1'
				]).', getOnlyHostParam()), {dialogue_class: "modal-popup-generic"});'
			)
			->setEnabled(!$readonly);

		// Select item prototype button.
		if ($data['parent_discoveryid'] !== null) {
			$yaxisMinData[] = (new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN);
			$yaxisMinData[] = (new CButton('yaxis_min_prototype', _('Select prototype')))
				->addClass(ZBX_STYLE_BTN_GREY)
				->onClick(
					'return PopUp("popup.generic", '.json_encode([
						'srctbl' => 'item_prototypes',
						'srcfld1' => 'itemid',
						'srcfld2' => 'name',
						'dstfrm' => $graphForm->getName(),
						'dstfld1' => 'ymin_itemid',
						'dstfld2' => 'ymin_name',
						'parent_discoveryid' => $data['parent_discoveryid'],
						'numeric' => '1'
					]).', {dialogue_class: "modal-popup-generic"});'
				)
				->setEnabled(!$readonly);
		}
	}
	else {
		$graphForm->addVar('yaxismin', $this->data['yaxismin']);
	}

	$yaxismin_label = new CLabel(_('Y axis MIN value'));
	if ($this->data['ymin_type'] == GRAPH_YAXIS_TYPE_ITEM_VALUE) {
		$yaxismin_label
			->setAsteriskMark()
			->setAttribute('for', 'ymin_name');
	}

	$graphFormList->addRow($yaxismin_label, $yaxisMinData);

	$yaxisMaxData = [];
	$yaxisMaxData[] = (new CSelect('ymax_type'))
		->setId('ymax_type')
		->setValue($this->data['ymax_type'])
		->addOptions(CSelect::createOptionsFromArray([
			GRAPH_YAXIS_TYPE_CALCULATED => _('Calculated'),
			GRAPH_YAXIS_TYPE_FIXED => _('Fixed'),
			GRAPH_YAXIS_TYPE_ITEM_VALUE => _('Item')
		]))
		->setDisabled($readonly);

	if ($this->data['ymax_type'] == GRAPH_YAXIS_TYPE_FIXED) {
		$yaxisMaxData[] = (new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN);
		$yaxisMaxData[] = (new CTextBox('yaxismax', $this->data['yaxismax'], $readonly))
			->setWidth(ZBX_TEXTAREA_SMALL_WIDTH);
	}
	elseif ($this->data['ymax_type'] == GRAPH_YAXIS_TYPE_ITEM_VALUE) {
		$graphForm->addVar('yaxismax', $this->data['yaxismax']);
		$yaxisMaxData[] = (new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN);
		$yaxisMaxData[] = (new CTextBox('ymax_name', $data['ymax_item_name'], true))
			->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired();
		$yaxisMaxData[] = (new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN);

		// Select item button.
		$yaxisMaxData[] = (new CButton('yaxis_max', _('Select')))
			->addClass(ZBX_STYLE_BTN_GREY)
			->onClick(
				'return PopUp("popup.generic", jQuery.extend('.json_encode([
					'srctbl' => 'items',
					'srcfld1' => 'itemid',
					'srcfld2' => 'name',
					'dstfrm' => $graphForm->getName(),
					'dstfld1' => 'ymax_itemid',
					'dstfld2' => 'ymax_name',
					'with_webitems' => '1',
					'numeric' => '1',
					'writeonly' => '1'
				]).', getOnlyHostParam()), {dialogue_class: "modal-popup-generic"});'
			)
			->setEnabled(!$readonly);

		// Select item prototype button.
		if ($data['parent_discoveryid'] !== null) {
			$yaxisMaxData[] = (new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN);
			$yaxisMaxData[] = (new CButton('yaxis_max_prototype', _('Select prototype')))
				->addClass(ZBX_STYLE_BTN_GREY)
				->onClick(
					'return PopUp("popup.generic", '.json_encode([
						'srctbl' => 'item_prototypes',
						'srcfld1' => 'itemid',
						'srcfld2' => 'name',
						'dstfrm' => $graphForm->getName(),
						'dstfld1' => 'ymax_itemid',
						'dstfld2' => 'ymax_name',
						'parent_discoveryid' => $data['parent_discoveryid'],
						'numeric' => '1'
					]).', {dialogue_class: "modal-popup-generic"});'
				)
				->setEnabled(!$readonly);
		}
	}
	else {
		$graphForm->addVar('yaxismax', $this->data['yaxismax']);
	}

	$yaxismax_label = new CLabel(_('Y axis MAX value'));
	if ($this->data['ymax_type'] == GRAPH_YAXIS_TYPE_ITEM_VALUE) {
		$yaxismax_label
			->setAsteriskMark()
			->setAttribute('for', 'ymax_name');
	}

	$graphFormList->addRow($yaxismax_label, $yaxisMaxData);
}
else {
	$graphFormList->addRow(_('3D view'),
		(new CCheckBox('show_3d'))
			->setChecked($this->data['show_3d'] == 1)
			->setEnabled(!$readonly)
	);
}

// Append items to form list.
$items_table = (new CTable())
	->setId('itemsTable')
	->setColumns([
		(new CTableColumn())->addClass('table-col-handle'),
		(new CTableColumn())->addClass('table-col-no'),
		(new CTableColumn(_('Name')))->addClass(($this->data['graphtype'] == GRAPH_TYPE_NORMAL)
			? 'table-col-name-normal'
			: 'table-col-name'
		),
		in_array($this->data['graphtype'], [GRAPH_TYPE_PIE, GRAPH_TYPE_EXPLODED])
			? (new CTableColumn(_('Type')))->addClass('table-col-type')
			: null,
		(new CTableColumn(_('Function')))->addClass('table-col-function'),
		($this->data['graphtype'] == GRAPH_TYPE_NORMAL)
			? (new CTableColumn(
				(new CColHeader(_('Draw style')))->addClass(ZBX_STYLE_NOWRAP)
			))
				->addClass('table-col-draw-style')
			: null,
		in_array($this->data['graphtype'], [GRAPH_TYPE_NORMAL, GRAPH_TYPE_STACKED])
			? (new CTableColumn(
				(new CColHeader(_('Y axis side')))->addClass(ZBX_STYLE_NOWRAP)
			))
				->addClass('table-col-y-axis-side')
			: null,
		(new CTableColumn(_('Color')))->addClass('table-col-colour'),
		$readonly ? null : (new CTableColumn(_('Action')))->addClass('table-col-action')
	]);

$parameters_add = [
	'srctbl' => 'items',
	'srcfld1' => 'itemid',
	'srcfld2' => 'name',
	'dstfrm' => $graphForm->getName(),
	'numeric' => '1',
	'writeonly' => '1',
	'multiselect' => '1',
	'with_webitems' => '1'
];
if ($data['normal_only']) {
	$parameters_add['normal_only'] = '1';
}
if ($data['hostid']) {
	$parameters_add['hostid'] = $data['hostid'];
}

$parameters_add_prototype = [
	'srctbl' => 'item_prototypes',
	'srcfld1' => 'itemid',
	'srcfld2' => 'name',
	'dstfrm' => $graphForm->getName(),
	'numeric' => '1',
	'writeonly' => '1',
	'multiselect' => '1',
	'graphtype' => $data['graphtype']
];
if ($data['normal_only']) {
	$parameters_add_prototype['normal_only'] = '1';
}
if ($data['parent_discoveryid']) {
	$parameters_add_prototype['parent_discoveryid'] = $data['parent_discoveryid'];
}

$items_table->addRow(
	(new CRow(
		$readonly
			? null
			: (new CCol(
				new CHorList([
					(new CButton('add_item', _('Add')))
						->onClick(
							'return PopUp("popup.generic",
								jQuery.extend('.json_encode($parameters_add).', getOnlyHostParam())
							);'
						)
						->addClass(ZBX_STYLE_BTN_LINK),
					$data['parent_discoveryid']
						? (new CButton('add_protoitem', _('Add prototype')))
							->onClick('return PopUp("popup.generic", '.json_encode($parameters_add_prototype).',
								{dialogue_class: "modal-popup-generic"}
							);')
							->addClass(ZBX_STYLE_BTN_LINK)
						: null
				])
			))->setColSpan(8)
	))->setId('itemButtonsRow')
);

foreach ($this->data['items'] as $n => $item) {
	$name = $item['host'].NAME_DELIMITER.$item['name'];

	if (zbx_empty($item['drawtype'])) {
		$item['drawtype'] = 0;
	}

	if (zbx_empty($item['yaxisside'])) {
		$item['yaxisside'] = 0;
	}

	if (!array_key_exists('gitemid', $item)) {
		$item['gitemid'] = '';
	}

	insert_js('loadItem('.$n.', '.json_encode($item['gitemid']).', '.$item['itemid'].', '.
		json_encode($name).', '.$item['type'].', '.$item['calc_fnc'].', '.$item['drawtype'].', '.
		$item['yaxisside'].', \''.$item['color'].'\', '.$item['flags'].');',
		true
	);
}

$graphFormList->addRow(
	(new CLabel(_('Items'), $items_table->getId()))->setAsteriskMark(),
	(new CDiv($items_table))->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR)
);

if ($data['parent_discoveryid']) {
	$graphFormList->addRow(_('Discover'),
		(new CCheckBox('discover', ZBX_PROTOTYPE_DISCOVER))
			->setChecked($data['discover'] == ZBX_PROTOTYPE_DISCOVER)
			->setUncheckedValue(ZBX_PROTOTYPE_NO_DISCOVER)
	);
}

// Append tabs to form.
$graphTab = (new CTabView())
	->setSelected(0)
	->addTab('graphTab', ($data['parent_discoveryid'] === null) ? _('Graph') : _('Graph prototype'), $graphFormList);

/*
 * Preview tab
 */
$graphPreviewTable = (new CTable())
	->addStyle('width: 100%;')
	->addRow(
		(new CRow(
			(new CDiv())->setId('previewChart')
		))->addClass(ZBX_STYLE_CENTER)
	);
$graphTab->addTab('previewTab', _('Preview'), $graphPreviewTable);

// Append buttons to form.
if ($data['graphid'] != 0) {
	$updateButton = new CSubmit('update', _('Update'));
	$deleteButton = new CButtonDelete(
		($data['parent_discoveryid'] === null) ? _('Delete graph?') : _('Delete graph prototype?'),
		url_params(['graphid', 'parent_discoveryid', 'hostid', 'context']), 'context'
	);

	if ($readonly) {
		$updateButton->setEnabled(false);
	}

	if ($is_templated) {
		$deleteButton->setEnabled(false);
	}

	$graphTab->setFooter(makeFormFooter(
		$updateButton, [
			new CSubmit('clone', _('Clone')),
			$deleteButton,
			new CButtonCancel(url_params(['parent_discoveryid', 'context']).url_param('hostid', $data['hostid']))
		]
	));
}
else {
	$graphTab->setFooter(makeFormFooter(
		new CSubmit('add', _('Add')),
		[new CButtonCancel(url_params(['parent_discoveryid', 'context']).url_param('hostid', $data['hostid']))]
	));
}

$graph_item_drawtypes = [];
foreach (graph_item_drawtypes() as $drawtype) {
	$graph_item_drawtypes[$drawtype] = graph_item_drawtype2str($drawtype);
}

// Insert js (depended from some variables inside the file).
require_once dirname(__FILE__).'/js/configuration.graph.edit.js.php';

$graphForm->addItem($graphTab);

// Append form to widget.
$widget->addItem($graphForm);

$widget->show();
