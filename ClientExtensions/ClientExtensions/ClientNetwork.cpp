#include "ClientNetwork.h"

#include "CustomPacketBuffer.h"
#include "Logger.h"
#include "ClientLua.h"

#include "CustomPacketDefines.h"

#include <map>
#include <sstream>

// note: most of the mess here is because I didn't
// figure out how to register userdata yet

struct ClientPacket;

CLIENT_METHOD(InitializePacket, 0x00401050, void, (ClientPacket* packet))
CLIENT_METHOD(FinalizePacket, 0x00401130, void, (ClientPacket* packet))
CLIENT_FUNC(SendPacket, 0x006B0B50, void, (ClientPacket* packet))

// The raw client packet
struct ClientPacket {
	uint32_t m_padding;
	uint8_t* m_buffer;
	uint32_t m_base;
	uint32_t m_alloc;
	uint32_t m_size;
	uint32_t m_read;
	ClientPacket(uint8_t* buffer, size_t size)
		: m_padding(0)
		, m_buffer(0)
		, m_size(0)
		, m_alloc(0)
		, m_base(0)
		, m_read(0)
	{
		InitializePacket(this);
		m_buffer = buffer;
		m_size = size;
		m_alloc = size;
	}
};


class ClientMessageWrite : public CustomPacketWrite
{
public:
	ClientMessageWrite()
		: CustomPacketWrite(0, MAX_FRAGMENT_SIZE,0)
	{}
	ClientMessageWrite(PACKET_OPCODE_TYPE opcode, size_t size)
		: CustomPacketWrite(opcode, MAX_FRAGMENT_SIZE,size)
	{}

	void Send()
	{
		std::vector<CustomPacketChunk>& chunks = buildMessages(1);
		for (auto& chunk : chunks)
		{

			// the API assumes the opcode is not a part of the payload,
			// because TrinityCore WorldPackets do not include it.
			//
			// This means just a single copy on the client.
			// Getting rid of it would be very cumbersome to almost no benefit.

			size_t fullSize = sizeof(uint32_t) + chunk.FullSize();
			char* c = new char[fullSize];
#pragma warning(push)
#pragma warning(disable: 6001)
			(*(uint32_t*)c[0]) = CLIENT_TO_SERVER_OPCODE;
#pragma warning(pop)
			memcpy(c + sizeof(uint32_t), chunk.Data(), chunk.FullSize());
			ClientPacket* p = new ClientPacket((uint8_t*)c, fullSize);
			FinalizePacket(p);
			SendPacket(p);
		}

		if (!IsPersistent())
		{
			Destroy();
		}
	}
};

template <typename T>
class MessageRegistry {
	// if you store 4 billion messages at once you'll have other problems
	uint32_t m_cur = 0;
public:
	std::map<uint32_t, T> m_map;
	uint32_t freeId()
	{
		while (m_map.find(m_cur) != m_map.end())
		{
			++m_cur;
		}
		return m_cur;
	}

	uint32_t add(T& value, T** ptr)
	{
		*ptr = &(m_map[freeId()] = value);
		return m_cur++;
	}

	T* get(uint32_t id)
	{
		auto itr = m_map.find(id);
		if (itr == m_map.end())
		{
			return nullptr;
		}
		return &itr->second;
	}
};

MessageRegistry<ClientMessageWrite> writes;
MessageRegistry<CustomPacketRead> reads;

class ClientMessageBuffer : public CustomPacketBuffer {
	virtual void OnPacket(CustomPacketRead* value) override final
	{
		uint32_t msg = reads.add(*value,&value);
		ClientLua::DoString(
			  ("__FireMessage(" + std::to_string(msg) + ")").c_str()
			, ClientLua::State()
		);
		if (!value->IsPersistent())
		{
			value->Destroy();
			reads.m_map.erase(msg);
		}
	}

	virtual void OnError(CustomPacketResult error) override final
	{
		LOG_ERROR << "Packet reading error " << uint32_t(error);
	}
};

template <typename T>
int WriteNum(lua_State* L)
{
	uint32_t id = uint32_t(ClientLua::GetNumber(L, 1, 0));
	T val = T(ClientLua::GetNumber(L, 2, 0));

	auto itr = writes.m_map.find(id);
	if (itr == writes.m_map.end())
	{
		// todo: error message
		return 0;
	}
	itr->second.Write(val);
	return 0;
}

// See lua polyfill in messages.lua
extern "C" {
	int _WriteMessage(lua_State* L)
	{
		PACKET_OPCODE_TYPE opcode(ClientLua::GetNumber(L, 1, 0));
		uint32_t size(ClientLua::GetNumber(L, 2, 0));
		uint32_t id = writes.freeId();
		writes.m_map[id] = ClientMessageWrite{opcode, size};
		ClientLua::PushNumber(L, id);
		return 1;
	}

	int _WriteUInt8(lua_State* L) { return WriteNum<uint8_t>(L); }

	int _SendMessage(lua_State* L) {
		uint32_t id( ClientLua::GetNumber(L, 1) );
		LOG_DEBUG << "Message being sent " << id;
		ClientMessageWrite* write = writes.get(id);
		if (write == nullptr)
		{
			// todo error message
			return 0;
		}
		write->Send();
		if (!write->IsPersistent())
		{
			writes.m_map.erase(id);
		}
		return 0;
	}
}

void ClientNetwork::initialize()
{
	ClientLua::AddFunction("_WriteUInt8", _WriteUInt8, __FILE__, __LINE__);
	ClientLua::AddFunction("_WriteMessage", _WriteMessage, __FILE__, __LINE__);
	ClientLua::AddFunction("_SendMessage", _SendMessage, __FILE__, __LINE__);
}
