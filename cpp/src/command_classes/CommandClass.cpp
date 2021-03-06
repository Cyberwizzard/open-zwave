//-----------------------------------------------------------------------------
//
//	CommandClass.cpp
//
//	Base class for all Z-Wave Command Classes
//
//	Copyright (c) 2010 Mal Lansell <openzwave@lansell.org>
//
//	SOFTWARE NOTICE AND LICENSE
//
//	This file is part of OpenZWave.
//
//	OpenZWave is free software: you can redistribute it and/or modify
//	it under the terms of the GNU Lesser General Public License as published
//	by the Free Software Foundation, either version 3 of the License,
//	or (at your option) any later version.
//
//	OpenZWave is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU Lesser General Public License for more details.
//
//	You should have received a copy of the GNU Lesser General Public License
//	along with OpenZWave.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------------

#include <math.h>
#include <locale.h>
#include "Defs.h"
#include "tinyxml.h"
#include "command_classes/CommandClass.h"
#include "command_classes/Basic.h"
#include "command_classes/MultiInstance.h"
#include "command_classes/CommandClasses.h"
#include "Msg.h"
#include "Node.h"
#include "Driver.h"
#include "Localization.h"
#include "Manager.h"
#include "platform/Log.h"
#include "value_classes/Value.h"
#include "value_classes/ValueStore.h"

namespace OpenZWave
{
	namespace Internal
	{
		namespace CC
		{

			static uint8 const c_sizeMask = 0x07;
			static uint8 const c_scaleMask = 0x18;
			static uint8 const c_scaleShift = 0x03;
			static uint8 const c_precisionMask = 0xe0;
			static uint8 const c_precisionShift = 0x05;

//-----------------------------------------------------------------------------
// <CommandClass::CommandClass>
// Constructor
//-----------------------------------------------------------------------------
			CommandClass::CommandClass(uint32 const _homeId, uint8 const _nodeId) :
					m_com(CompatOptionType_Compatibility, this), m_dom(CompatOptionType_Discovery, this), m_homeId(_homeId), m_nodeId(_nodeId), m_SecureSupport(true), m_sentCnt(0), m_receivedCnt(0)
			{
				m_com.EnableFlag(COMPAT_FLAG_GETSUPPORTED, true);
				m_com.EnableFlag(COMPAT_FLAG_OVERRIDEPRECISION, 0);
				m_com.EnableFlag(COMPAT_FLAG_FORCEVERSION, 0);
				m_com.EnableFlag(COMPAT_FLAG_CREATEVARS, true);
				m_com.EnableFlag(COMPAT_FLAG_REFRESHONWAKEUP, false);
				m_com.EnableFlag(COMPAT_FLAG_VERIFYCHANGED, false);
				m_com.EnableFlag(COMPAT_FLAG_NO_REFRESH_AFTER_SET, false);
				m_dom.EnableFlag(STATE_FLAG_CCVERSION, 0);
				m_dom.EnableFlag(STATE_FLAG_STATIC_REQUESTS, 0);
				m_dom.EnableFlag(STATE_FLAG_AFTERMARK, false);
				m_dom.EnableFlag(STATE_FLAG_ENCRYPTED, false);
				m_dom.EnableFlag(STATE_FLAG_INNIF, false);

			}

//-----------------------------------------------------------------------------
// <CommandClass::~CommandClass>
// Destructor
//-----------------------------------------------------------------------------
			CommandClass::~CommandClass()
			{
				while (!m_endPointMap.empty())
				{
					map<uint8, uint8>::iterator it = m_endPointMap.begin();
					m_endPointMap.erase(it);
				}
				while (!m_RefreshClassValues.empty())
				{
					multimap<uint16, RefreshValue *>::iterator it;
					for (it = m_RefreshClassValues.begin(); it != m_RefreshClassValues.end(); it++)
					{
						delete it->second;
					}
					m_RefreshClassValues.clear();
				}

			}

//-----------------------------------------------------------------------------
// <CommandClass::GetDriver>
// Get a pointer to our driver
//-----------------------------------------------------------------------------
			OpenZWave::Driver* CommandClass::GetDriver() const
			{
				return (Manager::Get()->GetDriver(m_homeId));
			}

//-----------------------------------------------------------------------------
// <CommandClass::GetNode>
// Get a pointer to our node without locking the mutex
//-----------------------------------------------------------------------------
			OpenZWave::Node* CommandClass::GetNodeUnsafe() const
			{
				return (GetDriver()->GetNodeUnsafe(m_nodeId));
			}

//-----------------------------------------------------------------------------
// <CommandClass::GetValue>
// Get a pointer to a value by its instance and index
//-----------------------------------------------------------------------------
			OpenZWave::Internal::VC::Value* CommandClass::GetValue(uint8 const _instance, uint16 const _index)
			{
				Internal::VC::Value* value = NULL;
				if (Node* node = GetNodeUnsafe())
				{
					value = node->GetValue(GetCommandClassId(), _instance, _index);
				}
				return value;
			}

//-----------------------------------------------------------------------------
// <CommandClass::RemoveValue>
// Remove a value by its instance and index
//-----------------------------------------------------------------------------
			bool CommandClass::RemoveValue(uint8 const _instance, uint16 const _index)
			{
				if (Node* node = GetNodeUnsafe())
				{
					return node->RemoveValue(GetCommandClassId(), _instance, _index);
				}
				return false;
			}

//-----------------------------------------------------------------------------
// <CommandClass::SetInstances>
// Instances as set by the MultiInstance V1 command class
//-----------------------------------------------------------------------------
			void CommandClass::SetInstances(uint8 const _instances)
			{
				// Ensure we have a set of reported variables for each new instance
				if (!m_dom.GetFlagBool(STATE_FLAG_AFTERMARK))
				{
					for (uint8 i = 0; i < _instances; ++i)
					{
						SetInstance(i + 1);
					}
				}
			}

//-----------------------------------------------------------------------------
// <CommandClass::SetInstance>
// Instances as set by the MultiChannel (i.e. MultiInstance V2) command class
//-----------------------------------------------------------------------------
			void CommandClass::SetInstance(uint8 const _endPoint)
			{
				if (!m_instances.IsSet(_endPoint))
				{
					m_instances.Set(_endPoint);
				}
			}

//-----------------------------------------------------------------------------
// <CommandClass::SetInstanceLabel>
// Set the Label for a Instance of this CommandClass
//-----------------------------------------------------------------------------
			void CommandClass::SetInstanceLabel(uint8 const _instance, char *label)
			{
				m_instanceLabel[_instance] = std::string(label);
			}
//-----------------------------------------------------------------------------
// <CommandClass::GetInstanceLabel>
// Get the Label for a Instance of this CommandClass
//-----------------------------------------------------------------------------

			std::string CommandClass::GetInstanceLabel(uint8 const _instance)
			{
				if (m_instanceLabel.count(_instance))
				{
					return Localization::Get()->GetGlobalLabel(m_instanceLabel[_instance]);
				}
				return std::string();
			}

//-----------------------------------------------------------------------------
// <CommandClass::ReadXML>
// Read the saved command class data
//-----------------------------------------------------------------------------
			void CommandClass::ReadXML(TiXmlElement const* _ccElement)
			{
				int32 intVal;
				char const* str;

				m_com.ReadXML(_ccElement);
				m_dom.ReadXML(_ccElement);

				// Apply any differences from the saved XML to the values
				TiXmlElement const* child = _ccElement->FirstChildElement();
				while (child)
				{
					str = child->Value();
					if (str)
					{
						if (!strcmp(str, "Instance"))
						{
							uint8 instance = 0;
							// Add an instance to the command class
							if (TIXML_SUCCESS == child->QueryIntAttribute("index", &intVal))
							{
								instance = (uint8) intVal;
								SetInstance(instance);
							}
							// See if its associated endpoint is present
							if (TIXML_SUCCESS == child->QueryIntAttribute("endpoint", &intVal))
							{
								uint8 endpoint = (uint8) intVal;
								SetEndPoint(instance, endpoint);
							}
							str = child->Attribute("label");
							if (str)
							{
								SetInstanceLabel(instance, (char *) str);
								Localization::Get()->SetGlobalLabel(str, str, "");
								TiXmlElement const *labellang = child->FirstChildElement();
								while (labellang)
								{
									char const* str2 = labellang->Value();
									if (str2 && !strcmp(str2, "Label"))
									{
										char const *lang = labellang->Attribute("lang");
										Localization::Get()->SetGlobalLabel(str, labellang->GetText(), lang);
									}
									labellang = labellang->NextSiblingElement();
								}
							}
						}
						else if (!strcmp(str, "Value"))
						{
							// Apply any differences from the saved XML to the value
							GetNodeUnsafe()->ReadValueFromXML(GetCommandClassId(), child);
						}
						else if (!strcmp(str, "TriggerRefreshValue"))
						{
							ReadValueRefreshXML(child);
						}
					}

					child = child->NextSiblingElement();
				}
				// Make sure previously created values are removed if create_vars=false
				if (!m_com.GetFlagBool(COMPAT_FLAG_CREATEVARS))
				{
					if (Node* node = GetNodeUnsafe())
					{
						node->GetValueStore()->RemoveCommandClassValues(GetCommandClassId());
					}
				}

			}

//-----------------------------------------------------------------------------
// <CommandClass::ReadValueRefreshXML>
// Read the config that contains a list of Values that should be refreshed when
// we recieve u updated Value from a device. (This helps Yale Door Locks, which send a
// Alarm Report instead of DoorLock Report when the status of the Door Lock is changed
//-----------------------------------------------------------------------------
			void CommandClass::ReadValueRefreshXML(TiXmlElement const* _ccElement)
			{

				char const* str;
				uint16 sourceIdx;
				bool ok = true;
				int temp;
				_ccElement->QueryIntAttribute("Index", &temp);
				sourceIdx = (uint16) temp;

				/* check if we have a entry already */
				if (m_RefreshClassValues.find(sourceIdx) != m_RefreshClassValues.end())
				{
						Log::Write(LogLevel_Warning, GetNodeId(), "TriggerRefreshValue - A Entry already exists for CC %s Index %d", GetCommandClassName().c_str(), sourceIdx);
						return;
				}


				Log::Write(LogLevel_Info, GetNodeId(), "Value Refresh triggered by CommandClass: %s, Index: %d for:", GetCommandClassName().c_str(), sourceIdx);
				TiXmlElement const* child = _ccElement->FirstChildElement();
				while (child)
				{
					ok = true;
					str = child->Value();
					if (str)
					{
						if (!strcmp(str, "RefreshClassValue"))
						{
							RefreshValue *arcc = new RefreshValue();
							if (child->QueryIntAttribute("CommandClass", &temp) != TIXML_SUCCESS)
							{
								Log::Write(LogLevel_Warning, GetNodeId(), "\tInvalid XML - CommandClass Attribute is wrong type or missing");
								child = child->NextSiblingElement();
								continue;
							}
							arcc->cc = (uint8) temp;
							if (child->QueryIntAttribute("RequestFlags", &temp) != TIXML_SUCCESS)
							{
								Log::Write(LogLevel_Warning, GetNodeId(), "\tInvalid XML - RequestFlags Attribute is wrong type or missing");
								child = child->NextSiblingElement();
								continue;
							}
							arcc->requestflags = (uint8) temp;
							if (child->QueryIntAttribute("Index", &temp) != TIXML_SUCCESS)
							{
								Log::Write(LogLevel_Warning, GetNodeId(), "\tInvalid XML - Index Attribute is wrong type or missing");
								child = child->NextSiblingElement();
								continue;
							}
							arcc->index = (uint16) temp;
							/* check for Duplicates */
							multimap<uint16, RefreshValue *>::iterator it;
							for (it = m_RefreshClassValues.begin(); it != m_RefreshClassValues.end(); it++)
							{
								uint16 idx = it->first;
								RefreshValue *rv = it->second;
								if ((idx == sourceIdx) && (rv->cc == arcc->cc) && (rv->requestflags == arcc->requestflags) && (rv->index == arcc->index))
								{
									Log::Write(LogLevel_Warning, GetNodeId(), "\tTarget Exists: CC %s Index %d", CommandClasses::GetName(arcc->cc).c_str(), arcc->index);
									delete arcc;
									ok = false;
									break;
								}
							}
							if (ok)
							{
								Log::Write(LogLevel_Info, GetNodeId(), "\tCommandClass: %s, RequestFlags: %d, Index: %d", CommandClasses::GetName(arcc->cc).c_str(), arcc->requestflags, arcc->index);
								m_RefreshClassValues.insert(std::make_pair(sourceIdx, arcc));
							}
						}
						else
						{
							Log::Write(LogLevel_Warning, GetNodeId(), "Got Unhandled Child Entry in TriggerRefreshValue XML Config: %s", str);
						}
					}
					child = child->NextSiblingElement();
				}
			}

//-----------------------------------------------------------------------------
// <CommandClass::CheckForRefreshValues>
// Scan our m_RefreshClassValues vector to see if we should call any other
// Command Classes to refresh their value
//-----------------------------------------------------------------------------

			bool CommandClass::CheckForRefreshValues(Internal::VC::Value const* _value)
			{
				/* if there are no values here... */
				if (m_RefreshClassValues.find(_value->GetID().GetIndex()) == m_RefreshClassValues.end())
					return false;

				Node* node = GetNodeUnsafe();
				if (node != NULL)
				{
					multimap<uint16, RefreshValue *>::iterator it;
					for (it = m_RefreshClassValues.find(_value->GetID().GetIndex()); it != m_RefreshClassValues.end(); it++)
					{
						RefreshValue *rcc = it->second;
						/* just to be sure we have the right index */
						if (it->first != _value->GetID().GetIndex())
							return false;
						Log::Write(LogLevel_Debug, GetNodeId(), "Requesting Refresh of Value: CommandClass: %s Instance %d, Index %d", CommandClasses::GetName(rcc->cc).c_str(), _value->GetID().GetInstance(), rcc->index);
						if (CommandClass* cc = node->GetCommandClass(rcc->cc))
						{
							cc->RequestValue(rcc->requestflags, rcc->index, _value->GetID().GetInstance(), Driver::MsgQueue_Send);
						}
					}
				}
				else /* Driver */
				{
					Log::Write(LogLevel_Warning, GetNodeId(), "Can't get Node");
				}
				return true;
			}

//-----------------------------------------------------------------------------
// <CommandClass::WriteXML>
// Save the static node configuration data
//-----------------------------------------------------------------------------
			void CommandClass::WriteXML(TiXmlElement* _ccElement)
			{
				char str[32];

				m_com.WriteXML(_ccElement);
				m_dom.WriteXML(_ccElement);

				snprintf(str, sizeof(str), "%d", GetCommandClassId());
				_ccElement->SetAttribute("id", str);
				_ccElement->SetAttribute("name", GetCommandClassName().c_str());

				// Write out the instances
				for (Bitfield::Iterator it = m_instances.Begin(); it != m_instances.End(); ++it)
				{
					TiXmlElement* instanceElement = new TiXmlElement("Instance");
					_ccElement->LinkEndChild(instanceElement);

					snprintf(str, sizeof(str), "%d", *it);
					instanceElement->SetAttribute("index", str);

					map<uint8, uint8>::iterator eit = m_endPointMap.find(*it);
					if (eit != m_endPointMap.end())
					{
						snprintf(str, sizeof(str), "%d", eit->second);
						instanceElement->SetAttribute("endpoint", str);
					}
					if (m_instanceLabel.count(*it) > 0)
						instanceElement->SetAttribute("label", GetInstanceLabel(*it).c_str());
				}

				// Write out the values for this command class
				Internal::VC::ValueStore* store = GetNodeUnsafe()->GetValueStore();
				for (Internal::VC::ValueStore::Iterator it = store->Begin(); it != store->End(); ++it)
				{
					Internal::VC::Value* value = it->second;
					if (value->GetID().GetCommandClassId() == GetCommandClassId())
					{
						TiXmlElement* valueElement = new TiXmlElement("Value");
						_ccElement->LinkEndChild(valueElement);
						value->WriteXML(valueElement);
					}
				}
				// Write out the TriggerRefreshValue if it exists
				multimap<uint16, RefreshValue *>::iterator it;
				uint16 sourceidx = 0;
				TiXmlElement* RefreshElement = nullptr;

				for (it = m_RefreshClassValues.begin(); it != m_RefreshClassValues.end(); it++)
				{
					RefreshValue *rcc = it->second;
					if (sourceidx != it->first) { 
						RefreshElement = new TiXmlElement("TriggerRefreshValue");
						_ccElement->LinkEndChild(RefreshElement);
						RefreshElement->SetAttribute("Index", it->first);
						sourceidx = it->first;
					}
					if (RefreshElement) {
						TiXmlElement *ClassElement = new TiXmlElement("RefreshClassValue");
						RefreshElement->LinkEndChild(ClassElement);
						ClassElement->SetAttribute("CommandClass", rcc->cc);
						ClassElement->SetAttribute("RequestFlags", rcc->requestflags);
						ClassElement->SetAttribute("Index", rcc->index);
					}
					else
					{
						Log::Write(LogLevel_Warning, GetNodeId(), "CommandClass::WriteXML: - RefreshElement was empty for index %d", it->first);
					}
				}
			}

//-----------------------------------------------------------------------------
// <CommandClass::ExtractValue>
// Read a value from a variable length sequence of bytes
//-----------------------------------------------------------------------------
			std::string CommandClass::ExtractValue(uint8 const* _data, uint8* _scale, uint8* _precision, uint8 _valueOffset // = 1
					) const
			{
				uint8 const size = _data[0] & c_sizeMask;
				uint8 const precision = (_data[0] & c_precisionMask) >> c_precisionShift;

				if (_scale)
				{
					*_scale = (_data[0] & c_scaleMask) >> c_scaleShift;
				}

				if (_precision)
				{
					*_precision = precision;
				}

				uint32 value = 0;
				uint8 i;
				for (i = 0; i < size; ++i)
				{
					value <<= 8;
					value |= (uint32) _data[i + (uint32) _valueOffset];
				}

				// Deal with sign extension.  All values are signed
				std::string res;
				if (_data[_valueOffset] & 0x80)
				{
					res = "-";

					// MSB is signed
					if (size == 1)
					{
						value |= 0xffffff00;
					}
					else if (size == 2)
					{
						value |= 0xffff0000;
					}
				}

				// Convert the integer to a decimal string.  We avoid
				// using floats to prevent accuracy issues.
				char numBuf[12] =
				{ 0 };

				if (precision == 0)
				{
					// The precision is zero, so we can just print the number directly into the string.
					snprintf(numBuf, 12, "%d", (signed int) value);
					res = numBuf;
				}
				else
				{
					// We'll need to insert a decimal point and include any necessary leading zeros.

					// Fill the buffer with the value padded with leading zeros.
					snprintf(numBuf, 12, "%011d", (signed int) value);

					// Calculate the position of the decimal point in the buffer
					int32 decimal = 10 - precision;

					// Shift the characters to make space for the decimal point.
					// We don't worry about overwriting any minus sign since that is
					// already written into the res string. While we're shifting, we
					// also look for the real starting position of the number so we
					// can copy it into the res string later.
					int32 start = -1;
					for (int32 i = 0; i < decimal; ++i)
					{
						numBuf[i] = numBuf[i + 1];
						if ((start < 0) && (numBuf[i] != '0'))
						{
							start = i;
						}
					}
					if (start < 0)
					{
						start = decimal - 1;
					}

					// Insert the decimal point
					struct lconv const* locale = localeconv();
					numBuf[decimal] = *(locale->decimal_point);

					// Copy the buffer into res
					res += &numBuf[start];
				}

				return res;
			}

//-----------------------------------------------------------------------------
// <CommandClass::decodeDuration>
// Decode the Duration Field to Seconds - CC:0000.00.00.11.016
//-----------------------------------------------------------------------------

			uint32 CommandClass::decodeDuration(uint8 data) const
			{
				if (data <= 0x7f)
					return data;
				if ((data > 0x7f) && (data <= 0xFD))
					return ((data - 0x7F)*60);
				 /* a 0xFE means Unknown Duration
				  * and 0xFF is Reserved - So lets return -1 (to wrap our Int)
				 */
				return -1;
			}

			uint8 CommandClass::encodeDuration(uint32 seconds) const
			{
				if (seconds <= 0x7f)
					return (seconds & 0xFF);
				/* 7620 seconds is the max that can fit into our scale, so anything above that, use it as the Default Duration 
				* its 7620 as we only can go upto 127 minutes - See https://github.com/OpenZWave/open-zwave/issues/1321#issuecomment-656532282 */ 
				if (seconds > 7620)
					return 0xFF;
				/* if we get here, seconds is always going to be at least 2 minutes - 0x7F(127) is > 2 minutes */
				return (uint8)0x79 + ((seconds/60) & 0xFF);
			}



//-----------------------------------------------------------------------------
// <CommandClass::AppendValue>
// Add a value to a message as a sequence of bytes
//-----------------------------------------------------------------------------
			void CommandClass::AppendValue(Msg* _msg, std::string const& _value, uint8 const _scale, uint8 const _minsize, uint8 const _minprecision) const
			{
				uint8 precision;
				uint8 size;
				int32 val = ValueToInteger(_value, &precision, &size, _minsize, _minprecision);

				_msg->Append((precision << c_precisionShift) | (_scale << c_scaleShift) | size);

				int32 shift = (size - 1) << 3;
				for (int32 i = size; i > 0; --i, shift -= 8)
				{
					_msg->Append((uint8) (val >> shift));
				}
			}

//-----------------------------------------------------------------------------
// <CommandClass::GetAppendValueSize>
// Get the number of bytes that would be added by a call to AppendValue
//-----------------------------------------------------------------------------
			uint8 const CommandClass::GetAppendValueSize(std::string const& _value, uint8 const _minsize, uint8 const _minprecision ) const
			{
				uint8 size;
				ValueToInteger(_value, NULL, &size, _minsize, _minprecision);
				return size;
			}

//-----------------------------------------------------------------------------
// <CommandClass::ValueToInteger>
// Convert a decimal string to an integer and report the precision and
// number of bytes required to store the value.
// If a minimum byte size or precisions is given, ensure the returned integer is using at least that precision and the reported o_size is at least _minsize
//-----------------------------------------------------------------------------
			int32 CommandClass::ValueToInteger(std::string const& _value, uint8* o_precision, uint8* o_size, uint8 const _minsize, uint8 const _minprecision) const
			{
				int32 val;
				uint8 precision;

				// Find the decimal point
				size_t pos = _value.find_first_of(".");
				if (pos == std::string::npos)
					pos = _value.find_first_of(",");

				if (pos == std::string::npos)
				{
					// No decimal point
					precision = 0;

					// Convert the string to an integer
					val = atol(_value.c_str());
				}
				else
				{
					// Remove the decimal point and convert to an integer
					precision = (uint8) ((_value.size() - pos) - 1);

					std::string str = _value.substr(0, pos) + _value.substr(pos + 1);
					val = atol(str.c_str());
				}

				uint8_t orp = m_com.GetFlagByte(COMPAT_FLAG_OVERRIDEPRECISION);
				if (orp == 0 && _minprecision > 0) orp = _minprecision; // Only if no override is given and a valid _minprecision is used, force the precision to _minprecision
				if (orp > 0)
				{
					while (precision < orp)
					{
						precision++;
						val *= 10;
					}
				}

				if (o_precision)
					*o_precision = precision;

				if (o_size)
				{
					// Work out the size as either 1, 2 or 4 bytes
					*o_size = 4;
					if (val < 0)
					{
						if ((val & 0xffffff80) == 0xffffff80)
						{
							*o_size = 1;
						}
						else if ((val & 0xffff8000) == 0xffff8000)
						{
							*o_size = 2;
						}
					}
					else
					{
						if ((val & 0xffffff00) == 0)
						{
							*o_size = 1;
						}
						else if ((val & 0xffff0000) == 0)
						{
							*o_size = 2;
						}
					}

					if(*o_size < _minsize && (_minsize == 1 || _minsize == 2 || _minsize == 4))
					  *o_size = _minsize;
				}


				return val;
			}

//-----------------------------------------------------------------------------
// <CommandClass::UpdateMappedClass>
// Update the mapped class if there is one with BASIC level
//-----------------------------------------------------------------------------
			void CommandClass::UpdateMappedClass(uint8 const _instance, uint8 const _classId, uint8 const _level)
			{
				if (_classId)
				{
					if (Node* node = GetNodeUnsafe())
					{
						CommandClass* cc = node->GetCommandClass(_classId);
						if (cc != NULL)
						{
							cc->SetValueBasic(_instance, _level);
						}
					}
				}
			}

//-----------------------------------------------------------------------------
// <CommandClass::ClearStaticRequest>
// The static data for this command class has been read from the device
//-----------------------------------------------------------------------------
			void CommandClass::ClearStaticRequest(uint8_t _request)
			{
				uint8_t f_staticRequests = m_dom.GetFlagByte(STATE_FLAG_STATIC_REQUESTS);
				f_staticRequests &= ~_request;
				m_dom.SetFlagByte(STATE_FLAG_STATIC_REQUESTS, f_staticRequests);
			}
//-----------------------------------------------------------------------------
// <CommandClass::ClearStaticRequest>
// The static data for this command class has been read from the device
//-----------------------------------------------------------------------------

			void CommandClass::SetStaticRequest(uint8_t _request)
			{
				uint8_t f_staticRequests = m_dom.GetFlagByte(STATE_FLAG_STATIC_REQUESTS);
				f_staticRequests |= _request;
				m_dom.SetFlagByte(STATE_FLAG_STATIC_REQUESTS, f_staticRequests);
			}

//-----------------------------------------------------------------------------
// <CommandClass::RequestStateForAllInstances>
// Request current state from the device
//-----------------------------------------------------------------------------
			bool CommandClass::RequestStateForAllInstances(uint32 const _requestFlags, Driver::MsgQueue const _queue)
			{
				bool res = false;
				if (m_com.GetFlagBool(COMPAT_FLAG_CREATEVARS))
				{
					if (Node* node = GetNodeUnsafe())
					{
						MultiInstance* multiInstance = static_cast<MultiInstance*>(node->GetCommandClass(MultiInstance::StaticGetCommandClassId()));
						if (multiInstance != NULL)
						{
							for (Bitfield::Iterator it = m_instances.Begin(); it != m_instances.End(); ++it)
							{
								res |= RequestState(_requestFlags, (uint8) *it, _queue);
							}
						}
						else
						{
							res = RequestState(_requestFlags, 1, _queue);
						}
					}
				}

				return res;
			}

//-----------------------------------------------------------------------------
// <CommandClass::RequestStateForAllInstances>
// Request current state from the device
//-----------------------------------------------------------------------------
			std::string const CommandClass::GetCommandClassLabel()
			{
				return m_commandClassLabel;
			}

//-----------------------------------------------------------------------------
// <CommandClass::RequestStateForAllInstances>
// Request current state from the device
//-----------------------------------------------------------------------------
			void CommandClass::SetCommandClassLabel(std::string label)
			{
				m_commandClassLabel = label;
			}

//-----------------------------------------------------------------------------
// <CommandClass::SetVersion>
// Made out-of-line to allow overriding
//-----------------------------------------------------------------------------
			void CommandClass::SetVersion(uint8 const _version)
			{
				if (m_com.GetFlagByte(COMPAT_FLAG_FORCEVERSION) == 0)
				{
					if (_version >= m_dom.GetFlagByte(STATE_FLAG_CCVERSION))
					{
						m_dom.SetFlagByte(STATE_FLAG_CCVERSION, _version);
					}
					else
					{
						Log::Write(LogLevel_Warning, GetNodeId(), "Trying to Downgrade Command Class %s version from %d to %d. Ignored", GetCommandClassName().c_str(), m_dom.GetFlagByte(STATE_FLAG_CCVERSION), _version);
					}
				}
				else
				{
					m_dom.SetFlagByte(STATE_FLAG_CCVERSION, m_com.GetFlagByte(COMPAT_FLAG_FORCEVERSION));
					Log::Write(LogLevel_Warning, GetNodeId(), "Attempt to update Command Class %s version from %d to %d. Ignored", GetCommandClassName().c_str(), m_dom.GetFlagByte(STATE_FLAG_CCVERSION), _version);
				}

			}

//-----------------------------------------------------------------------------
// <CommandClass::refreshValuesOnWakeup>
// Refresh Values upon Wakeup Notification
//-----------------------------------------------------------------------------
			void CommandClass::refreshValuesOnWakeup()
			{
				if (m_com.GetFlagBool(COMPAT_FLAG_REFRESHONWAKEUP))
				{
					Log::Write(LogLevel_Debug, GetNodeId(), "Refreshing Dynamic Values on Wakeup for CommandClass %s", GetCommandClassName().c_str());
					RequestStateForAllInstances(CommandClass::RequestFlag_Dynamic, Driver::MsgQueue_Send);
				}
			}

//-----------------------------------------------------------------------------
// <CommandClass::HandleIncomingMsg>
// Handles Messages for Controlling CC's (Not Controlled, which is the default)
//-----------------------------------------------------------------------------
			bool CommandClass::HandleIncomingMsg(uint8 const* _data, uint32 const _length, uint32 const _instance)
			{
				Log::Write(LogLevel_Warning, GetNodeId(), "Routing HandleIncomingMsg to HandleMsg - Please Report: %s ", GetCommandClassName().c_str());
				return HandleMsg(_data, _length, _instance);
			}
//-----------------------------------------------------------------------------
// <CommandClass::CreateVars>
// Calls CreateVars on the CC for each instance registered with this CC
//-----------------------------------------------------------------------------
			void CommandClass::CreateVars()
			{
				if (m_com.GetFlagBool(COMPAT_FLAG_CREATEVARS))
				{
					for (Bitfield::Iterator it = m_instances.Begin(); it != m_instances.End(); ++it)
					{
						Log::Write(LogLevel_Info, GetNodeId(), "Creating ValueIDs for Instance %d on %s", (uint8)*it, GetCommandClassLabel().c_str());
						CreateVars((uint8) *it);
					}
				}
			}

//-----------------------------------------------------------------------------
// <CommandClass::CreateVars>
// Create ValueID's for Specific Instances. CC's should override this. 
//-----------------------------------------------------------------------------
			void CommandClass::CreateVars(uint8 const _instance)
			{
			}


		} // namespace CC
	} // namespace Internal
} // namespace OpenZWave

