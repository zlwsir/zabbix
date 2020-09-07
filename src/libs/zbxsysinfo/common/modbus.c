/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
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

#include "common.h"
#include "mbustype.h"
#include "mutexs.h"
#include "comms.h"

#ifdef HAVE_LIBMODBUS

zbx_mutex_t	modbus_lock = ZBX_MUTEX_NULL;

#define LOCK_MODBUS	zbx_mutex_lock(modbus_lock)
#define UNLOCK_MODBUS	zbx_mutex_unlock(modbus_lock)

#define ZBX_MODBUS_DATATYPE_STRLEN_MAX	6

#define ZBX_MODBUS_BAUDRATE_DEFAULT	115200
#define ZBX_MODBUS_ADDRESS_MAX		65535

#ifdef _WINDOWS
#pragma comment(lib, "modbus")
#endif

static struct modbus_datatype_ref
{
	modbus_datatype_t	datatype;
	char			datatype_str[ZBX_MODBUS_DATATYPE_STRLEN_MAX + 1];
}
modbus_datatype_map[] =
{
	{ ZBX_MODBUS_DATATYPE_BIT,	"bit" },
	{ ZBX_MODBUS_DATATYPE_INT8,	"int8" },
	{ ZBX_MODBUS_DATATYPE_UINT8,	"uint8" },
	{ ZBX_MODBUS_DATATYPE_INT16,	"int16" },
	{ ZBX_MODBUS_DATATYPE_UINT16,	"uint16" },
	{ ZBX_MODBUS_DATATYPE_INT32,	"int32" },
	{ ZBX_MODBUS_DATATYPE_UINT32,	"uint32" },
	{ ZBX_MODBUS_DATATYPE_FLOAT,	"float" },
	{ ZBX_MODBUS_DATATYPE_UINT64,	"uint64" },
	{ ZBX_MODBUS_DATATYPE_DOUBLE,	"double" }
};

static uint64_t	read_reg_64(uint16_t *reg16, modbus_endianness_t endianness)
{
	switch(endianness)
	{
		case ZBX_MODBUS_ENDIANNESS_BE:
			return ZBX_MODBUS_64BE(reg16);
		case ZBX_MODBUS_ENDIANNESS_LE:
			return ZBX_MODBUS_64LE(reg16);
		case ZBX_MODBUS_ENDIANNESS_MBE:
			return ZBX_MODBUS_64MBE(reg16);
		case ZBX_MODBUS_ENDIANNESS_MLE:
			return ZBX_MODBUS_64MLE(reg16);
	}

	THIS_SHOULD_NEVER_HAPPEN;
	return 0;
}

static uint32_t	read_reg_32(uint16_t *reg16, modbus_endianness_t endianness)
{
	switch(endianness)
	{
		case ZBX_MODBUS_ENDIANNESS_BE:
			return ZBX_MODBUS_32BE(reg16);
		case ZBX_MODBUS_ENDIANNESS_LE:
			return ZBX_MODBUS_32LE(reg16);
		case ZBX_MODBUS_ENDIANNESS_MBE:
			return ZBX_MODBUS_32MBE(reg16);
		case ZBX_MODBUS_ENDIANNESS_MLE:
			return ZBX_MODBUS_32MLE(reg16);
	}

	THIS_SHOULD_NEVER_HAPPEN;
	return 0;
}

static uint16_t	read_reg_16(uint16_t *reg16, modbus_endianness_t endianness)
{
	if (ZBX_MODBUS_ENDIANNESS_LE == endianness)
		return ZBX_MODBUS_BYTE_SWAP_16(*reg16);
	else
		return *reg16;
}

static uint8_t	read_reg_8_most(uint16_t *reg16, modbus_endianness_t endianness)
{
	return (uint8_t)(ZBX_MODBUS_ENDIANNESS_BE == endianness ?
			MODBUS_GET_HIGH_BYTE(*reg16) : MODBUS_GET_LOW_BYTE(*reg16));
}

static uint8_t	read_reg_8_less(uint16_t *reg16, modbus_endianness_t endianness)
{
	return (uint8_t)(ZBX_MODBUS_ENDIANNESS_BE == endianness ?
			MODBUS_GET_LOW_BYTE(*reg16) : MODBUS_GET_HIGH_BYTE(*reg16));
}


static void	set_serial_params_default(zbx_modbus_connection_serial *serial_params)
{
	serial_params->data_bits = 8;
	serial_params->parity = ZBX_MODBUS_SERIAL_PARAMS_PARITY_NONE;
	serial_params->stop_bits = 1;
}

/******************************************************************************
 *                                                                            *
 * Function: get_total_count                                                  *
 *                                                                            *
 * Purpose: get total number of bytes to read from device                     *
 *                                                                            *
 * Parameters: count  - [IN] count of sequenced same data type values to      *
 *                               be read from device                          *
 *             offset - [IN] number of registers to be discarded              *
 *             type   - [IN] data type                                        *
 *                                                                            *
 * Return value: total number of bytes                                        *
 *                                                                            *
 ******************************************************************************/
static unsigned int	get_total_count(unsigned short count, unsigned short offset, modbus_datatype_t type)
{
	unsigned int	total_count;

	switch(type)
	{
		case ZBX_MODBUS_DATATYPE_BIT:
			total_count = (count - 1) / 9;
			break;
		case ZBX_MODBUS_DATATYPE_INT8:
		case ZBX_MODBUS_DATATYPE_UINT8:
			total_count = count;
			break;

		case ZBX_MODBUS_DATATYPE_INT32:
		case ZBX_MODBUS_DATATYPE_UINT32:
		case ZBX_MODBUS_DATATYPE_FLOAT:
			total_count = count * 4;
			break;
		case ZBX_MODBUS_DATATYPE_UINT64:
		case ZBX_MODBUS_DATATYPE_DOUBLE:
			total_count = count * 8;
			break;
		default:
			total_count = count * 2;
			break;
	}

	return total_count + offset * 2;
}

/******************************************************************************
 *                                                                            *
 * Function: parse_params                                                     *
 *                                                                            *
 * Purpose: parse serial connection parameters                                *
 *                                                                            *
 * Parameters: params        - [IN] string holding parameters                 *
 *             serial_params - [OUT] parsed parameters                        *
 *                                                                            *
 * Return value: SUCCEED - parameters parsed successfully                     *
 *               FAIL    - failed to parse parameters                         *
 *                                                                            *
 ******************************************************************************/
static int	parse_params(char *params, zbx_modbus_connection_serial *serial_params)
{
	if (0 == isdigit(params[0]) || 0 == isdigit(params[2]))
		return FAIL;

	serial_params->data_bits = (unsigned char)params[0] - '0';

	if (5 > serial_params->data_bits || serial_params->data_bits > 8)
		return FAIL;

	serial_params->stop_bits = (unsigned char)params[2] - '0';

	if (1 > serial_params->stop_bits || serial_params->stop_bits > 2)
		return FAIL;

	serial_params->parity = toupper(params[1]);

	if (ZBX_MODBUS_SERIAL_PARAMS_PARITY_NONE != serial_params->parity &&
			ZBX_MODBUS_SERIAL_PARAMS_PARITY_EVEN != serial_params->parity &&
			ZBX_MODBUS_SERIAL_PARAMS_PARITY_ODD != serial_params->parity)
	{
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: endpoint_parse                                                   *
 *                                                                            *
 * Purpose: parse endpoint                                                    *
 *                                                                            *
 * Parameters: endpoint_str - [IN] string holding endpoint                    *
 *             endpoint     - [OUT] parsed endpoint                           *
 *                                                                            *
 * Return value: SUCCEED - endpoint parsed successfully                       *
 *               FAIL    - failed to parse endpoint                           *
 *                                                                            *
 ******************************************************************************/
static int	endpoint_parse(char *endpoint_str, zbx_modbus_endpoint_t *endpoint)
{
#define ZBX_MODBUS_PROTOCOL_PREFIX_TCP	"tcp://"
#define ZBX_MODBUS_PROTOCOL_PREFIX_RTU	"rtu://"
	char	*ptr, *tmp = NULL;
	int	ret = SUCCEED;

	if (0 == zbx_strncasecmp(endpoint_str, ZBX_MODBUS_PROTOCOL_PREFIX_TCP,
			ZBX_CONST_STRLEN(ZBX_MODBUS_PROTOCOL_PREFIX_TCP)))
	{
		unsigned short	port;

		endpoint->protocol = ZBX_MODBUS_PROTOCOL_TCP;
		ptr = endpoint_str + ZBX_CONST_STRLEN(ZBX_MODBUS_PROTOCOL_PREFIX_TCP);

		if (SUCCEED == (ret = parse_serveractive_element(ptr, &tmp, &port, ZBX_MODBUS_TCP_PORT_DEFAULT)))
		{
			endpoint->conn_info.tcp.ip = tmp;
			endpoint->conn_info.tcp.port = zbx_dsprintf(NULL, "%u", port);
		}
	}
	else if (0 == zbx_strncasecmp(endpoint_str, ZBX_MODBUS_PROTOCOL_PREFIX_RTU,
			ZBX_CONST_STRLEN(ZBX_MODBUS_PROTOCOL_PREFIX_RTU)))
	{
		endpoint->protocol = ZBX_MODBUS_PROTOCOL_RTU;
		ptr = endpoint_str + ZBX_CONST_STRLEN(ZBX_MODBUS_PROTOCOL_PREFIX_RTU);

		if (NULL != (tmp = strchr(ptr, ':')))
		{
			size_t	alloc_len = 0, offset = 0;
			char	*baudrate_str;

			endpoint->conn_info.serial.port = NULL;
			zbx_strncpy_alloc(&endpoint->conn_info.serial.port, &alloc_len, &offset, ptr, tmp - ptr);
			zbx_strsplit(++tmp, ':', &baudrate_str, &ptr);
			endpoint->conn_info.serial.baudrate = atoi(baudrate_str);
			zbx_free(baudrate_str);

			if (NULL != ptr)
			{
				if (3 == strlen(ptr))
					ret = parse_params(ptr, &endpoint->conn_info.serial);
				else
					ret = FAIL;

				if (SUCCEED != ret)
					zbx_free(endpoint->conn_info.serial.port);

				zbx_free(ptr);
			}
			else
				set_serial_params_default(&endpoint->conn_info.serial);
		}
		else
		{
			endpoint->conn_info.serial.port = zbx_strdup(NULL, ptr);
			endpoint->conn_info.serial.baudrate = ZBX_MODBUS_BAUDRATE_DEFAULT;
			set_serial_params_default(&endpoint->conn_info.serial);
		}

#if !(defined(_WINDOWS) || defined(__MINGW32__))
		endpoint->conn_info.serial.port = zbx_dsprintf(endpoint->conn_info.serial.port, "/dev/%s",
				endpoint->conn_info.serial.port);
#endif
	}
	else
		ret = FAIL;

	return ret;
#undef ZBX_MODBUS_PROTOCOL_PREFIX_TCP
#undef ZBX_MODBUS_PROTOCOL_PREFIX_RTU
}

/******************************************************************************
 *                                                                            *
 * Function: modbus_read_data                                                 *
 *                                                                            *
 * Purpose: request and read modbus data                                      *
 *                                                                            *
 * Parameters: endpoint    - [IN] endpoint                                    *
 *             slaveid     - [IN] slave id                                    *
 *             function    - [IN] function                                    *
 *             address     - [IN] address of first register/coil/DI to read   *
 *             count       - [IN] count of sequenced same data type values to *
 *                                be read from device                         *
 *             type        - [IN] data type                                   *
 *             endianness  - [IN] endianness                                  *
 *             offset      - [IN] number of registers to be discarded         *
 *             total_count - [IN] total number of bytes to read               *
 *             res         - [OUT] retrieved modbus data                      *
 *             error       - [OUT] error message in case of failure           *
 *                                                                            *
 * Return value: SUCCEED - modbus data obtained successfully                  *
 *               FAIL    - failed to request or read modbus data              *
 *                                                                            *
 ******************************************************************************/
static int	modbus_read_data(zbx_modbus_endpoint_t *endpoint, unsigned char slaveid, unsigned char function,
		unsigned short address, unsigned short count, modbus_datatype_t type, modbus_endianness_t endianness,
		unsigned short offset, unsigned short total_count, AGENT_RESULT *res, char **error)
{

	modbus_t	*mdb_ctx;
	uint8_t		*dest8 = NULL;
	uint16_t	*dest16 = NULL;
	unsigned short	dest_sz;
	char		*list;
	int		i, ret = FAIL;

	if (ZBX_MODBUS_PROTOCOL_RTU == endpoint->protocol)
	{
		mdb_ctx = modbus_new_rtu(endpoint->conn_info.serial.port, endpoint->conn_info.serial.baudrate,
				endpoint->conn_info.serial.parity, endpoint->conn_info.serial.data_bits,
				endpoint->conn_info.serial.stop_bits);
	}
	else
		mdb_ctx = modbus_new_tcp_pi(endpoint->conn_info.tcp.ip, endpoint->conn_info.tcp.port);

	if (NULL == mdb_ctx)
	{
		*error = zbx_dsprintf(*error, "modbus_new_%s() failed: %s",
				ZBX_MODBUS_PROTOCOL_TCP == endpoint->protocol ? "tcp" : "rtu", modbus_strerror(errno));
		return FAIL;
	}

	if (0 != modbus_set_slave(mdb_ctx, slaveid))
	{
		*error = zbx_dsprintf(*error, "modbus_set_slave() failed: %s", modbus_strerror(errno));
		goto out;
	}

#if defined(HAVE_LIBMODBUS_3_0)
	{
		struct timeval	tv;

		tv.tv_sec = CONFIG_TIMEOUT;
		tv.tv_usec = 0;
		modbus_set_response_timeout(mdb_ctx, &tv);
	}
#else /* HAVE_LIBMODBUS_3_1 at the moment */
	if (0 !=  modbus_set_response_timeout(mdb_ctx, CONFIG_TIMEOUT, 0))
	{
		*error = zbx_dsprintf(*error, "modbus_set_response_timeout() failed: %s", modbus_strerror(errno));
		goto out;
	}
#endif

	if (ZBX_MODBUS_DATATYPE_BIT != type)
	{
		dest_sz = (total_count - 1) / 2 + 1;
		dest16 = zbx_malloc(NULL, sizeof(uint16_t) * dest_sz);
	}
	else
	{
		dest_sz = count + offset * ZBX_MODBUS_REGISTER_SZ;
		dest8 = zbx_malloc(NULL, sizeof(uint8_t) * dest_sz);
	}

	LOCK_MODBUS;

	if (0 !=  modbus_connect(mdb_ctx))
	{
		*error = zbx_dsprintf(*error, "modbus_connect() failed: %s", modbus_strerror(errno));
		UNLOCK_MODBUS;
		goto out;
	}

	switch(function)
	{
		case ZBX_MODBUS_FUNCTION_COIL:
			ret = modbus_read_bits(mdb_ctx, address, dest_sz, dest8);
			break;
		case ZBX_MODBUS_FUNCTION_DISCRETE_INPUT:
			ret = modbus_read_input_bits(mdb_ctx, address, dest_sz, dest8);
			break;
		case ZBX_MODBUS_FUNCTION_INPUT_REGISTERS:
			ret = modbus_read_input_registers(mdb_ctx, address, dest_sz, dest16);
			break;
		case ZBX_MODBUS_FUNCTION_HOLDING_REGISTERS:
			ret = modbus_read_registers(mdb_ctx, address, dest_sz, dest16);
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			modbus_close(mdb_ctx);
			UNLOCK_MODBUS;
			*error = zbx_strdup(*error, "invalid function");
			goto out;
	}

	modbus_close(mdb_ctx);
	UNLOCK_MODBUS;

	if (-1 == ret)
	{
		*error = zbx_dsprintf(*error, "modbus_read failed: %s", modbus_strerror(errno));
		goto out;
	}

	if (ZBX_MODBUS_DATATYPE_BIT == type)
	{
		uint8_t	*buf8;

		buf8 = dest8 + offset * ZBX_MODBUS_REGISTER_SZ;

		if (1 < count)
		{
			list = zbx_strdup(NULL, "[");

			for (i = 0; i < count; i++)
				list = zbx_dsprintf(list, "%s%s%u", list, 0 == i ? "" : ",", buf8[i]);

			list = zbx_dsprintf(list, "%s]", list);
			SET_STR_RESULT(res, list);
		}
		else
			SET_UI64_RESULT(res, *buf8);
	}
	else
	{
		uint16_t	*buf16;
		uint32_t	val_uint32;
		uint64_t	val_uint64;
		float		val_float;
		double		val_double;

		buf16 = dest16 + offset;

		if (1 == count)
		{
			switch(type)
			{
				case ZBX_MODBUS_DATATYPE_UINT8:
					SET_UI64_RESULT(res, read_reg_8_most(buf16, endianness));
					break;
				case ZBX_MODBUS_DATATYPE_INT8:
					SET_DBL_RESULT(res, (int8_t)read_reg_8_most(buf16, endianness));
					break;
				case ZBX_MODBUS_DATATYPE_UINT32:
					SET_UI64_RESULT(res, read_reg_32(buf16, endianness));
					break;
				case ZBX_MODBUS_DATATYPE_INT32:
					SET_DBL_RESULT(res, (int32_t)read_reg_32(buf16, endianness));
					break;
				case ZBX_MODBUS_DATATYPE_FLOAT:
					val_uint32 = read_reg_32(buf16, endianness);
					memcpy(&val_float, &val_uint32, sizeof(float));
					SET_DBL_RESULT(res, val_float);
					break;
				case ZBX_MODBUS_DATATYPE_UINT64:
					SET_UI64_RESULT(res, read_reg_64(buf16, endianness));
					break;
				case ZBX_MODBUS_DATATYPE_DOUBLE:
					val_uint64 = read_reg_64(buf16, endianness);
					memcpy(&val_double, &val_uint64, sizeof(double));
					SET_DBL_RESULT(res, val_double);
					break;
				case ZBX_MODBUS_DATATYPE_UINT16:
					SET_UI64_RESULT(res, read_reg_16(buf16, endianness));
					break;
				case ZBX_MODBUS_DATATYPE_INT16:
					SET_DBL_RESULT(res, (int16_t)read_reg_16(buf16, endianness));
					break;
				default:
					THIS_SHOULD_NEVER_HAPPEN;
					*error = zbx_strdup(*error, "internal error: unexpected data type");
					goto out;
			}
		}
		else
		{
			list = zbx_strdup(NULL, "[");

			for (i = 0; i < count; i++)
			{
				switch(type)
				{
					case ZBX_MODBUS_DATATYPE_UINT8:
						list = zbx_dsprintf(list, "%s%s%u", list, 0 == i ? "" : ",",
								read_reg_8_most(buf16, endianness));

						if (++i < count)
							break;

						list = zbx_dsprintf(list, "%s,%u", list,
								read_reg_8_less(buf16, endianness));
						buf16++;
						break;
					case ZBX_MODBUS_DATATYPE_INT8:
						list = zbx_dsprintf(list, "%s%s%d", list, 0 == i ? "" : ",",
								(int8_t)read_reg_8_most(buf16, endianness));

						if (++i < count)
							break;

						list = zbx_dsprintf(list, "%s,%d", list,
								(int8_t)read_reg_8_less(buf16, endianness));
						buf16++;
						break;
					case ZBX_MODBUS_DATATYPE_UINT32:
						list = zbx_dsprintf(list, "%s%s%u", list, 0 == i ? "" : ",",
								read_reg_32(buf16, endianness));
						buf16 += 2;
						break;
					case ZBX_MODBUS_DATATYPE_INT32:
						list = zbx_dsprintf(list, "%s%s%i", list, 0 == i ? "" : ",",
								(int32_t)read_reg_32(buf16, endianness));
						buf16 += 2;
						break;
					case ZBX_MODBUS_DATATYPE_FLOAT:
						val_uint32 = read_reg_32(buf16, endianness);
						memcpy(&val_float, &val_uint32, sizeof(float));
						list = zbx_dsprintf(list, "%s%s%f", list, 0 == i ? "" : ",", val_float);
						buf16 += 2;
						break;
					case ZBX_MODBUS_DATATYPE_UINT64:
						list = zbx_dsprintf(list, "%s%s%lu", list, 0 == i ? "" : ",",
								read_reg_64(buf16, endianness));
						buf16 += 4;
						break;
					case ZBX_MODBUS_DATATYPE_DOUBLE:
						val_uint64 = read_reg_64(buf16, endianness);
						memcpy(&val_double, &val_uint64, sizeof(double));
						list = zbx_dsprintf(list, "%s%s%g", list, 0 == i ? "" : ",", val_double);
						break;
					case ZBX_MODBUS_DATATYPE_UINT16:
						list = zbx_dsprintf(list, "%s%s%hu", list, 0 == i ? "" : ",",
								read_reg_16(buf16, endianness));
						buf16++;
						break;
					case ZBX_MODBUS_DATATYPE_INT16:
						list = zbx_dsprintf(list, "%s%s%hi", list, 0 == i ? "" : ",",
								(int16_t)read_reg_16(buf16, endianness));
						buf16++;
						break;
					default:
						THIS_SHOULD_NEVER_HAPPEN;
						zbx_free(list);
						*error = zbx_strdup(*error, "internal error: unexpected data type");
						goto out;
				}
			}

			list = zbx_dsprintf(list, "%s]", list);
			SET_STR_RESULT(res, list);
		}
	}

	ret = SUCCEED;
out:
	modbus_free(mdb_ctx);
	zbx_free(dest8);
	zbx_free(dest16);

	return ret;
}
#endif /* HAVE_LIBMODBUS */

int	MODBUS_GET(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#ifdef HAVE_LIBMODBUS
	char			*tmp, *err = NULL;
	unsigned char		slaveid, function, type;
	unsigned short		count, address, offset;
	unsigned int		total_count;
	zbx_modbus_endpoint_t	endpoint;
	modbus_endianness_t	endianness;
	int			ret = SYSINFO_RET_FAIL;

	if (8 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if (1 > request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* endpoint */
	if (FAIL == endpoint_parse(get_rparam(request, 0), &endpoint))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* slave id */
	if (NULL == (tmp = get_rparam(request, 1)) || '\0' == *tmp)
	{
		slaveid = ZBX_MODBUS_PROTOCOL_TCP == endpoint.protocol ? 255 : 1;
	}
	else if (FAIL == is_uint_n_range(tmp, ZBX_SIZE_T_MAX, &slaveid, sizeof(unsigned char),
			ZBX_MODBUS_PROTOCOL_TCP == endpoint.protocol ? 0 : 1,
			ZBX_MODBUS_PROTOCOL_TCP == endpoint.protocol ? 255 : 247))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		goto err;
	}

	/* function */
	if (NULL == (tmp = get_rparam(request, 2)) || '\0' == *tmp)
	{
		function = ZBX_MODBUS_FUNCTION_EMPTY;
	}
	else if (FAIL == is_uint_n_range(tmp, ZBX_SIZE_T_MAX, &function, sizeof(unsigned char), 1, 4))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		goto err;
	}

	/* address (and update function) */
	if (NULL == (tmp = get_rparam(request, 3)) || '\0' == *tmp)
	{
		address = 0;

		if (ZBX_MODBUS_FUNCTION_EMPTY == function)
			function = ZBX_MODBUS_FUNCTION_COIL;
	}
	else if (FAIL == is_ushort(tmp, &address))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fourth parameter."));
		goto err;
	}
	else if (ZBX_MODBUS_FUNCTION_EMPTY == function)
	{
		if (1 <= address && address <= 9999)
		{
			function = ZBX_MODBUS_FUNCTION_COIL;
			address -= 1;
		}
		else if (10001 <= address && address <= 19999)
		{
			function = ZBX_MODBUS_FUNCTION_DISCRETE_INPUT;
			address -= 10001;
		}
		else if (30001 <= address && address <= 39999)
		{
			function = ZBX_MODBUS_FUNCTION_INPUT_REGISTERS;
			address -= 30001;
		}
		else if (40001 <= address && address <= 49999)
		{
			function = ZBX_MODBUS_FUNCTION_HOLDING_REGISTERS;
			address -= 40001;
		}
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Unsupported address for the specified function."));
			goto err;
		}
	}

	/* count */
	if (NULL == (tmp = get_rparam(request, 4)) || '\0' == *tmp)
	{
		count = 1;
	}
	else if (FAIL == is_ushort(tmp, &count) || 0 == count)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fifth parameter."));
		goto err;
	}

	/* data type */
	if (NULL == (tmp = get_rparam(request, 5)) || '\0' == *tmp)
	{
		if (ZBX_MODBUS_FUNCTION_COIL == function || ZBX_MODBUS_FUNCTION_DISCRETE_INPUT == function)
			type = ZBX_MODBUS_DATATYPE_BIT;
		else
			type = ZBX_MODBUS_DATATYPE_UINT16;
	}
	else
	{
		size_t	i;

		for (i = 0; i < ARRSIZE(modbus_datatype_map); i++)
		{
			if (0 == strcmp(modbus_datatype_map[i].datatype_str, tmp))
			{
				type = modbus_datatype_map[i].datatype;
				break;
			}
		}

		if (ARRSIZE(modbus_datatype_map) == i)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid sixth parameter."));
			goto err;
		}

		if ((ZBX_MODBUS_DATATYPE_BIT == type && (ZBX_MODBUS_FUNCTION_INPUT_REGISTERS == function ||
				ZBX_MODBUS_FUNCTION_HOLDING_REGISTERS == function)) ||
				(ZBX_MODBUS_DATATYPE_BIT != type && (ZBX_MODBUS_FUNCTION_COIL == function ||
				ZBX_MODBUS_FUNCTION_DISCRETE_INPUT == function)))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Unsupported data type for the specified function."));
			goto err;
		}
	}

	/* endianness */
	if (NULL == (tmp = get_rparam(request, 6)) || '\0' == *tmp)
	{
		endianness = ZBX_MODBUS_ENDIANNESS_BE;
	}
	else
	{
		char	*endianness_l;

		endianness_l = zbx_strdup(NULL, tmp);
		zbx_strlower(endianness_l);

		if (0 == strcmp(endianness_l, "be"))
		{
			endianness = ZBX_MODBUS_ENDIANNESS_BE;
		}
		else if (0 == strcmp(endianness_l, "le"))
		{
			endianness = ZBX_MODBUS_ENDIANNESS_LE;
		}
		else if (0 == strcmp(endianness_l, "mbe"))
		{
			endianness = ZBX_MODBUS_ENDIANNESS_MBE;
		}
		else if (0 == strcmp(endianness_l, "mle"))
		{
			endianness = ZBX_MODBUS_ENDIANNESS_MLE;
		}
		else
		{
			zbx_free(endianness_l);
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid seventh parameter."));
			goto err;
		}

		zbx_free(endianness_l);
	}

	if ((ZBX_MODBUS_ENDIANNESS_LE == endianness && ZBX_MODBUS_DATATYPE_BIT == type) ||
			((ZBX_MODBUS_ENDIANNESS_MBE == endianness || ZBX_MODBUS_ENDIANNESS_MLE == endianness) &&
			(ZBX_MODBUS_DATATYPE_UINT16 == type || ZBX_MODBUS_DATATYPE_INT16 == type ||
			ZBX_MODBUS_DATATYPE_UINT8 == type || ZBX_MODBUS_DATATYPE_INT8 == type ||
			ZBX_MODBUS_DATATYPE_BIT == type)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unsupported endianness for the specified data type."));
		goto err;
	}

	/* offset */
	if (NULL == (tmp = get_rparam(request, 7)) || '\0' == *tmp)
	{
		offset = 0;
	}
	else if (FAIL == is_ushort(tmp, &offset))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid eighth parameter."));
		goto err;
	}

	/* total count */
	if (ZBX_MODBUS_ADDRESS_MAX < (total_count = get_total_count(count, offset, type)) + address)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid total count."));
		goto err;
	}

	if (SUCCEED != modbus_read_data(&endpoint, slaveid, function, address, count, type, endianness, offset,
			(unsigned short)total_count, result, &err))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot read modbus data: %s.", err));
		goto err;
	}

	ret = SYSINFO_RET_OK;
err:
	if (ZBX_MODBUS_PROTOCOL_TCP == endpoint.protocol)
	{
		zbx_free(endpoint.conn_info.tcp.ip);
		zbx_free(endpoint.conn_info.tcp.port);
	}
	else if (ZBX_MODBUS_PROTOCOL_RTU == endpoint.protocol)
		zbx_free(endpoint.conn_info.serial.port);

	return ret;
#else
	ZBX_UNUSED(request);
	ZBX_UNUSED(result);
	return SYSINFO_RET_FAIL;
#endif /* HAVE_LIBMODBUS */
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_init_modbus                                                  *
 *                                                                            *
 * Purpose: create modbus mutex                                               *
 *                                                                            *
 * Parameters: error      - [OUT] error message in case of failure            *
 *                                                                            *
 * Return value: SUCCEED - modbus mutex created successfully                  *
 *               FAIL    - failed to create modbus mutex                      *
 *                                                                            *
 ******************************************************************************/
int zbx_init_modbus(char **error)
{
#ifdef HAVE_LIBMODBUS
#	ifdef _WINDOWS
		return zbx_mutex_create(&modbus_lock, NULL, error);
#	else
		return zbx_mutex_create(&modbus_lock, ZBX_MUTEX_MODBUS, error);
#	endif
#else
	ZBX_UNUSED(error);
	return SUCCEED;
#endif
}
