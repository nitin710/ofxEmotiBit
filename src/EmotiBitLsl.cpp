#include "EmotiBitLsl.h"

static const string MARKER_INFO_NAME_LABEL = "name";
static const string MARKER_INFO_SOURCE_ID_KEY_LABEL = "sourceId";

EmotiBitLsl::ReturnCode EmotiBitLsl::addMarkerInput(string jsonStr)
{

	Json::Reader reader;
	Json::Value settings;
	reader.parse(jsonStr, settings);

	MarkerStreamInfo markerStreamInfo;

	if (!settings.isMember("lsl"))
	{
		_lastErrMsg = "[EmotiBitLsl::addMarkerInput] JSON tag not found: lsl";
		return ReturnCode::ERR_TAG_NOT_FOUND;
	}
	if (!settings["lsl"].isMember("marker"))
	{
		_lastErrMsg = "[EmotiBitLsl::addMarkerInput] JSON tag not found: marker";
		return ReturnCode::ERR_TAG_NOT_FOUND;
	}
	if (!settings["lsl"]["marker"].isMember(MARKER_INFO_NAME_LABEL))
	{
		_lastErrMsg = "[EmotiBitLsl::addMarkerInput] JSON tag not found: " + MARKER_INFO_NAME_LABEL;
		return ReturnCode::ERR_TAG_NOT_FOUND;
	}
	markerStreamInfo.name = settings["lsl"]["marker"][MARKER_INFO_NAME_LABEL].asString();
	if (settings["lsl"]["marker"].isMember(MARKER_INFO_SOURCE_ID_LABEL))
	{
		markerStreamInfo.sourceId = settings["lsl"]["marker"][MARKER_INFO_SOURCE_ID_LABEL].asString();
	}
	
	if (!markerStreamInfo.sourceId.empty())
	{
		markerStreamInfo.receiver = std::make_shared<ofxLSL::Receiver<string>>(markerStreamInfo.name, markerStreamInfo.sourceId);
	}
	else
	{
		markerStreamInfo.receiver = std::make_shared<ofxLSL::Receiver<string>>(markerStreamInfo.name);
	}

	_markerInputs.push_back(markerStreamInfo);

	return ReturnCode::SUCCESS;
}

EmotiBitLsl::ReturnCode EmotiBitLsl::addDataStreamOutputs(string jsonStr, string sourceId)
{
	Json::Reader reader;
	Json::Value settings;
	reader.parse(jsonStr, settings);
	
	// Parse patchboard
	PatchboardJson patchboard;
	PatchboardJson::ReturnCode status = patchboard.parse(jsonStr);
	if (status < 0)
	{
		_lastErrMsg = patchboard.getLastErrMsg();
		return (EmotiBitLsl::ReturnCode) status; // ToDo: consider if error checking is needed for status cast
	}
	if (patchboard.inputType != "EmotiBit")
	{
		_lastErrMsg = "[EmotiBitLsl::addDataStreamOutputs] Supported patchboard input types: EmotiBit";
		return ReturnCode::ERR_VALUE_MISMATCH;
	}
	if (patchboard.outputType != "LSL")
	{
		_lastErrMsg = "[EmotiBitLsl::addDataStreamOutputs] Supported patchboard output types: LSL";
		return ReturnCode::ERR_VALUE_MISMATCH;
	}

	// Parse patchboard settings
	if (!settings["patchboard"].isMember("settings"))
	{
		_lastErrMsg = "[EmotiBitLsl::addDataStreamOutputs] JSON tag not found: settings";
		return ReturnCode::ERR_TAG_NOT_FOUND;
	}
	if (!settings["patchboard"]["settings"].isMember("output"))
	{
		_lastErrMsg = "[EmotiBitLsl::addDataStreamOutputs] JSON tag not found: settings>output";
		return ReturnCode::ERR_TAG_NOT_FOUND;
	}
	if (!settings["patchboard"]["settings"]["output"].isMember("meta-data"))
	{
		_lastErrMsg = "[EmotiBitLsl::addDataStreamOutputs] JSON tag not found: meta-data";
		return ReturnCode::ERR_TAG_NOT_FOUND;
	}
	if (!settings["patchboard"]["settings"]["output"]["meta-data"].isMember("channels"))
	{
		_lastErrMsg = "[EmotiBitLsl::addDataStreamOutputs] JSON tag not found: channels";
		return ReturnCode::ERR_TAG_NOT_FOUND;
	}
	if (!settings["patchboard"]["settings"]["output"]["meta-data"]["channels"].isArray())
	{
		_lastErrMsg = "[EmotiBitLsl::addDataStreamOutputs] Incorrect JSON structure: channels must be an array";
		return ReturnCode::ERR_FORMAT_INCORRECT;
	}
	int nChan = settings["patchboard"]["settings"]["output"]["meta-data"]["channels"].size();
	if (nChan <= 0)
	{
		_lastErrMsg = "[EmotiBitLsl::addDataStreamOutputs] Incorrect JSON structure: channels must be an array";
		return ReturnCode::ERR_FORMAT_INCORRECT;
	}
	for (int i = 0; i < nChan; i++)
	{
		if (settings["patchboard"]["settings"]["output"]["meta-data"]["channels"][i].isMember("name"))
		{
			_lastErrMsg = "[EmotiBitLsl::addDataStreamOutputs] JSON tag not found: name";
			return ReturnCode::ERR_TAG_NOT_FOUND;
		}
		if (settings["patchboard"]["settings"]["output"]["meta-data"]["channels"][i].isMember("type"))
		{
			_lastErrMsg = "JSON tag not found: type";
			return ReturnCode::ERR_TAG_NOT_FOUND;
		}
		if (settings["patchboard"]["settings"]["output"]["meta-data"]["channels"][i].isMember("nominal_srate"))
		{
			_lastErrMsg = "[EmotiBitLsl::addDataStreamOutputs] JSON tag not found: nominal_srate";
			return ReturnCode::ERR_TAG_NOT_FOUND;
		}
		string name = settings["patchboard"]["settings"]["output"]["meta-data"]["channels"][i]["name"].asString();
		string type = settings["patchboard"]["settings"]["output"]["meta-data"]["channels"][i]["type"].asString();
		double nominal_srate = settings["patchboard"]["settings"]["output"]["meta-data"]["channels"][i]["nominal_srate"].asDouble();
		_lslSender.addStream(name, type, 1, nominal_srate, lsl::cf_float32, sourceId);
		_outChanTypeMap[std::make_pair(sourceId, name)] = type;
	}
	_patchboards[sourceId] = patchboard;
}

size_t EmotiBitLsl::getNumDataOutputs(string sourceId)
{
	auto iter = _patchboards.find(sourceId);
	if (iter == _patchboards.end())
	{
		return 0;
	}
	return iter->second.getNumPatches();
}

void EmotiBitLsl::clearDataStreamOutputs()
{
	_patchboards.clear();
	_outChanTypeMap.clear();
	_lslSender = ofxLSL::Sender();
}

size_t EmotiBitLsl::getNumMarkerInputs()
{
	return _markerInputs.size();
}

vector<string> EmotiBitLsl::createMarkerInputPackets(uint16_t &packetCounter)
{
	vector<string> packets;
	for (int j = 0; j < _markerInputs.size(); j++)
	{
		if (_markerInputs.at(j).receiver->isConnected())
		{
			// ToDo: consider moving marker receiver->flush() to a separate thread to tighten up timing
			auto markerSamples = _markerInputs.at(j).receiver->flush();
			// for all samples received
			for (int i = 0; i < markerSamples.size(); i++)
			{
				_markerInputs.at(j).rxCount;
				auto markerSample = markerSamples.at(i);
				std::stringstream payload;
				payload.precision(7);
				uint16_t payloadLen = 0;

				addToPayload(EmotiBitPacket::PayloadLabel::LSL_MARKER_RX_TIMESTAMP, payload, payloadLen);
				addToPayload(markerSample->timeStamp + markerSample->timeCorrection, payload, payloadLen);
				addToPayload(EmotiBitPacket::PayloadLabel::LSL_MARKER_SRC_TIMESTAMP, payload, payloadLen);
				addToPayload(markerSample->timeStamp, payload, payloadLen);
				addToPayload(EmotiBitPacket::PayloadLabel::LSL_LOCAL_CLOCK_TIMESTAMP, payload, payloadLen);
				addToPayload(lsl::local_clock(), payload, payloadLen);
				addToPayload(EmotiBitPacket::PayloadLabel::LSL_MARKER_DATA, payload, payloadLen);
				for (auto channel : markerSample->sample) 
				{
					addToPayload(channel, payload, payloadLen);
				}
				packets.push_back(EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::LSL_MARKER, packetCounter++, payload.str(), payloadLen));

				// ToDo: consider if we even need a marker sample to get cross domain points.
				//		Can we just not do: LC = lsl::local_clock, and LM = lsl::local_clock() - timeCorretion()?
				// send TX packet for LCxLM
				payload.clear();
				payloadLen = 0;
				addToPayload(EmotiBitPacket::PayloadLabel::LSL_LOCAL_CLOCK_TIMESTAMP, payload, payloadLen);
				addToPayload(markerSample->timeStamp + markerSample->timeCorrection, payload, payloadLen);
				addToPayload(EmotiBitPacket::PayloadLabel::LSL_MARKER_SRC_TIMESTAMP, payload, payloadLen);
				addToPayload(markerSample->timeStamp, payload, payloadLen);
				packets.push_back(EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::TIMESTAMP_CROSS_TIME, packetCounter++, payload.str(), payloadLen));


				// ToDo: Consider if TIMESTAMP_CROSS_TIME packet sending needs to be in a different spot
				double lsltime = lsl::local_clock();
				string timestampLocal = ofGetTimestampString(EmotiBitPacket::TIMESTAMP_STRING_FORMAT);
				payload.clear();
				payloadLen = 0;
				addToPayload(EmotiBitPacket::TypeTag::TIMESTAMP_LOCAL, payload, payloadLen);
				addToPayload(timestampLocal, payload, payloadLen);
				addToPayload(EmotiBitPacket::PayloadLabel::LSL_LOCAL_CLOCK_TIMESTAMP, payload, payloadLen);
				addToPayload(lsltime, payload, payloadLen);
				packets.push_back(EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::TIMESTAMP_CROSS_TIME, packetCounter++, payload.str(), payloadLen));
			}
		}
	}
}

template <class T>
void EmotiBitLsl::addToPayload(const T &element, std::stringstream &payload, uint16_t &payloadLen)
{
	payload << element << EmotiBitPacket::PAYLOAD_DELIMITER;
	payloadLen++;
}

string EmotiBitLsl::getlastErrMsg()
{
	return _lastErrMsg;
}

vector<EmotiBitLsl::MarkerStreamInfo> EmotiBitLsl::getMarkerStreamInfo()
{
	return _markerInputs;
}

template <class T>
bool EmotiBitLsl::addSample(const vector<T> &_values, const std::string &typeTag, const std::string &sourceId)
{
	auto iterP = _patchboards.find(source_id);
	if (iterN == _patchboards.end())
	{
		return false;
	}
	Patchboard patchboard = iterN->second;

	auto iterN = patchboard.find(typeTag);
	if (iterN == patchboard.end())
	{
		return false;
	}
	string name = iterN->second;

	auto iterT = _outChanTypeMap.find(make_pair(sourceId, name));
	if (iterT == _outChanTypeMap.end())
	{
		return false;
	}
	string type = iterT->second;

	_lslSender(values, name, type, _outputSourceId);

	return true;
}

bool EmotiBitLsl::isDataStreamOutputSource(string sourceId)
{
	return _patchboards.find(sourceId) != _patchboards.end();
}