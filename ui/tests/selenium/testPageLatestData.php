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

require_once dirname(__FILE__).'/../include/CWebTest.php';

/**
 * @backup history_uint
 */
class testPageLatestData extends CWebTest {

	public function testPageLatestData_CheckLayout() {
		$this->page->login()->open('zabbix.php?action=latest.view');
		$this->page->assertTitle('Latest data');
		$this->page->assertHeader('Latest data');
		$form = $this->query('name:zbx_filter')->asForm()->one();
		$this->assertEquals(['Host groups', 'Hosts', 'Name', 'Tags', 'Show details'],
				$form->getLabels()->asText());
		$this->assertTrue($form->query('xpath:.//label[text()="Show items without data"]')->one()->isValid());

		// Show item without data is checked/disabled without Hosts in filter and checked/enabled with Hosts in filter.
		foreach ([false, true] as $status) {
			$form->query('id:filter_show_without_data')->one()->isEnabled($status);
			$form->query('id:filter_show_without_data')->one()->isAttributePresent('checked');
			if (!$status) {
				$form->fill(['Hosts' => 'Host 1 from first group']);
			}
		}

		// Check filter buttons.
		foreach (['Apply', 'Reset'] as $button) {
			$this->assertTrue($form->query('button', $button)->one()->isClickable());
		}

		// Check table headers.
		$table = $this->query('class:list-table')->asTable()->one();
		$this->assertEquals(['', 'Host', 'Name', 'Last check', 'Last value', 'Change', 'Tags', '', 'Info'],
				$table->getHeadersText());

		// Check that sortable headers are clickable.
		foreach (['Host', 'Name'] as $header) {
			$this->assertTrue($table->query('xpath:.//th/a[text()="'.$header.'"]')->one()->isClickable());
		}

		// Check filter collapse/expand.
		$filter_tab = $this->query('xpath://a[contains(@class, "filter-trigger")]')->one();
		foreach ([true, false] as $status) {
			$this->assertEquals($status, $this->query('xpath://div[contains(@class, "filter-container")]')->one()->isDisplayed());
			$filter_tab->click();
		}
	}

	// Check that no real host or template names displayed.
	public function testPageLatestData_NoHostNames() {
		$result = CDBHelper::getAll(
			'SELECT host'.
			' FROM hosts'.
			' WHERE status IN ('.HOST_STATUS_MONITORED.', '.HOST_STATUS_NOT_MONITORED.', '.HOST_STATUS_TEMPLATE.')'.
				' AND name <> host'
		);
		$this->page->login()->open('zabbix.php?action=latest.view');
		$table = $this->query('class:list-table')->asTable()->one();
		foreach ($result as $hostname) {
			$this->assertFalse($table->query('xpath://td/a[text()='.CXPathHelper::escapeQuotes($hostname['host']).']')
					->one(false)->isDisplayed());
		}
	}

	public static function getItemDescription() {
		return [
			// Item without description.
			[
				[
					'hostid' => '15003',
					'Item name' => 'item_testPageHistory_CheckLayout_Log'
				]
			],
			// Item with plain text in the description.
			[
				[
					'hostid' => '15003',
					'Item name' => 'item_testPageHistory_CheckLayout_Log_2',
					'description' => 'Non-clickable description'
				]
			],
			// Item with only 1 url in description.
			[
				[
					'hostid' => '15003',
					'Item name' => 'item_testPageHistory_CheckLayout_Eventlog',
					'description' => 'https://zabbix.com'
				]
			],
			// Item with text and url in description.
			[
				[
					'hostid' => '15003',
					'Item name' => 'item_testPageHistory_CheckLayout_Eventlog_2',
					'description' => 'The following url should be clickable: https://zabbix.com'
				]
			],
			// Item with multiple urls in description.
			[
				[
					'hostid' => '15003',
					'Item name' => 'item_testPageHistory_CheckLayout_Character',
					'description' => 'http://zabbix.com https://www.zabbix.com/career https://www.zabbix.com/contact'
				]
			],
			// Item with text and 2 urls in description.
			[
				[
					'hostid' => '15003',
					'Item name' => 'item_testPageHistory_CheckLayout_Text',
					'description' => 'These urls should be clickable: https://zabbix.com https://www.zabbix.com/career'
				]
			],
			// Item with underscore in macros name and one non existing macros  in description .
			[
				[
					'hostid' => '50010',
					'Item name' => 'Http agent item form',
					'description' => 'Underscore {$NONEXISTING}'
				]
			],
			// Item with 2 macros in description.
			[
				[
					'hostid' => '50010',
					'Item name' => 'Http agent item for update',
					'description' => '127.0.0.1 Some text'
				]
			],
			// Item with 2 macros and text in description.
			[
				[
					'hostid' => '50010',
					'Item name' => 'Http agent item for delete',
					'description' => 'Some text and IP number 127.0.0.1'
				]
			],
			// Item with macros inside curly brackets.
			[
				[
					'hostid' => '50007',
					'Item name' => 'Item-layout-test-002',
					'description' => '{Some text}'
				]
			],
			// Item with macros in description.
			[
				[
					'hostid' => '99027',
					'Item name' => 'Item to check graph',
					'description' => 'Some text'
				]
			]
		];
	}

	/**
	 * @dataProvider getItemDescription
	 */
	public function testPageLatestData_checkItemDescription($data) {
		// Open Latest data for host 'testPageHistory_CheckLayout'
		$this->page->login()->open('zabbix.php?&action=latest.view&show_details=0&hostids%5B%5D='.$data['hostid']);
		$table_path = '//table['.CXPathHelper::fromClass('overflow-ellipsis').']';
		$table = $this->query('xpath', $table_path)->asTable()->one();

		// Find rows from the data provider and click on the description icon if such should persist.
		$row = $table->findRow('Name', $data['Item name'], true);

		if (CTestArrayHelper::get($data,'description', false)) {
			$row->query('class:icon-description')->one()->click()->waitUntilReady();
			$overlay = $this->query('xpath://div[@class="overlay-dialogue"]')->one();

			// Verify the real description with the expected one.
			$this->assertEquals($data['description'], $overlay->getText());

			// Get urls form description.
			$urls = [];
			preg_match_all('/https?:\/\/\S+/', $data['description'], $urls);
			// Verify that each of the urls is clickable.
			foreach ($urls[0] as $url) {
				$this->assertTrue($overlay->query('xpath:./div/a[@href="'.$url.'"]')->one()->isClickable());
			}

			// Verify that the tool-tip can be closed.
			$overlay->query('xpath:./button[@title="Close"]')->one()->click();
			$this->assertFalse($overlay->isDisplayed());
		}
		// If the item has no description the description icon should not be there.
		else {
			$this->assertTrue($row->query('class:icon-description')->count() === 0);
		}
	}

	/**
	 * Maintenance icon hintbox.
	 */
	public function testPageLatestData_checkMaintenanceIcon() {
		$this->page->login()->open('zabbix.php?action=latest.view');
		$form = $this->query('name:zbx_filter')->asForm()->one();
		$form->fill(['Hosts' => 'Available host in maintenance']);
		$form->submit();

		// TODO: change forceClick after ZBX-20426 merge.
		$this->query('xpath://span[contains(@class, "icon-maint")]')->one()->forceClick();
		$hint = $this->query('xpath://div[@data-hintboxid]')->asOverlayDialog()->waitUntilPresent()->all()->last()->getText();
		$hint_text = "Maintenance for Host availability widget [Maintenance with data collection]\n".
				"Maintenance for checking Show hosts in maintenance option in Host availability widget";
		$this->assertEquals($hint_text, $hint);
	}

	/**
	 * Check hint text for Last check and Last value columns
	 */
	public function testPageLatestData_checkHints() {
		$itemid = CDBHelper::getValue('SELECT itemid FROM items WHERE name='.zbx_dbstr('4_item'));
		$time = time();
		$value = '15';
		DBexecute('INSERT INTO history_uint (itemid, clock, value, ns) VALUES ('.zbx_dbstr($itemid).
				', '.zbx_dbstr($time).', '.zbx_dbstr($value).', 0)');
		$true_time = date("Y-m-d H:i:s", $time);
		$this->page->login()->open('zabbix.php?action=latest.view');
		$form = $this->query('name:zbx_filter')->asForm()->one();
		$form->query('button:Reset')->one()->click();
		$form->fill(['Name' => '4_item'])->submit();
		$table = $this->query('class:list-table')->asTable()->one();

		foreach (['Last check', 'Last value'] as $column) {
			if ($column === 'Last value') {
				$this->assertEquals('15 UNIT', $table->getRow(0)->getColumn($column)->getText());
			}
			$table->getRow(0)->getColumn($column)->query('class:cursor-pointer')->one()->click();
			$hint = $this->query('xpath://div[@data-hintboxid]')->asOverlayDialog()->waitUntilPresent()->all()->last()->getText();
			$compare_hint = ($column === 'Last check') ? $true_time : $value;
			$this->assertEquals($compare_hint, $hint);
		}
	}
}
