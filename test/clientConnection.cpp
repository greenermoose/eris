#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "clientConnection.h"
#include "stubServer.h"
#include "objectSummary.h"
#include "testUtils.h"

#include <Eris/LogStream.h>
#include <Eris/Exceptions.h>
#include <Atlas/Objects/Encoder.h>
#include <Atlas/Objects/RootOperation.h>
#include <Atlas/Objects/Operation.h>

using Atlas::Objects::Root;
using Atlas::Objects::smart_dynamic_cast;
using namespace Atlas::Objects::Operation;
using namespace Eris;
using Atlas::Objects::Entity::RootEntity;

typedef Atlas::Objects::Entity::Player AtlasPlayer;

ClientConnection::ClientConnection(StubServer* ss, int socket) :
    m_stream(socket),
    m_server(ss),
    m_codec(NULL),
    m_encoder(NULL)
{
    m_acceptor = new Atlas::Net::StreamAccept("Eris Stub Server", m_stream, this);
    m_acceptor->poll(false);
}

ClientConnection::~ClientConnection()
{
    delete m_encoder;
    delete m_acceptor;
    delete m_codec;
}

void ClientConnection::poll()
{
    if (m_acceptor)
    {
        negotiate();
    }
    else
    {
        m_codec->poll();
        
        while (!m_objDeque.empty())
        {
            RootOperation op = smart_dynamic_cast<RootOperation>(m_objDeque.front());
            if (!op.isValid())
                throw InvalidOperation("ClientConnection recived something that isn't an op");
            
            dispatch(op);
            m_objDeque.pop_front();
        }
    }
}

#pragma mark -
// basic Atlas connection / stream stuff

void ClientConnection::fail()
{
    m_stream.close();
    // tell the stub server to kill us off
}

void ClientConnection::negotiate()
{
    m_acceptor->poll(); 
    
    switch (m_acceptor->getState()) {
    case Atlas::Net::StreamAccept::IN_PROGRESS:
        break;

    case Atlas::Net::StreamAccept::FAILED:
        error() << "ClientConnection got Atlas negotiation failure";
        fail();
        break;

    case Atlas::Net::StreamAccept::SUCCEEDED:
        m_codec = m_acceptor->getCodec();
        m_encoder = new Atlas::Objects::ObjectsEncoder(*m_codec);
        m_codec->streamBegin();
                
        delete m_acceptor;
        m_acceptor = NULL;
        break;

    default:
        throw InvalidOperation("unknown state from Atlas StreamAccept in ClientConnection::negotiate");
    }             
}

void ClientConnection::objectArrived(const Root& obj)
{
    m_objDeque.push_back(obj);
}

#pragma mark -
// dispatch / decode logic

void ClientConnection::dispatch(const RootOperation& op)
{    
    const std::vector<Root>& args = op->getArgs();
    
    if (op->getFrom().empty())
    {        
        if (m_account.empty())
        {
            // not logged in yet
            Login login = smart_dynamic_cast<Login>(op);
            if (login.isValid())
            {
                processLogin(login);
                return;
            }
       
            Create cr = smart_dynamic_cast<Create>(op);
            if (cr.isValid())
            {
                assert(!args.empty());
                processAccountCreate(cr);
                return;
            }
        }
        
        Get get = smart_dynamic_cast<Get>(op);
        if (get.isValid())
        {
            processAnonymousGet(get);
            return;
        }
        
        throw TestFailure("got anonymous op I couldn't dispatch");
    }
    
    if (op->getFrom() == m_account)
    {
        dispatchOOG(op);
        return;
    }
    
    debug() << "totally failed to handle operation " << objectSummary(op);
}

void ClientConnection::dispatchOOG(const RootOperation& op)
{
    Look lk = smart_dynamic_cast<Look>(op);
    if (lk.isValid())
    {
        processOOGLook(lk);
        return;
    }
    
    Move mv = smart_dynamic_cast<Move>(op);
    if (mv.isValid())
    {
        // deocde part vs join
        // issue command
        return;
    }

    Talk tk = smart_dynamic_cast<Talk>(op);
    if (tk.isValid())
    {
    
    
        return;
    }
    
    Imaginary imag = smart_dynamic_cast<Imaginary>(op);
    if (imag.isValid())
    {
    
        return;
    }
}

void ClientConnection::processLogin(const Login& login)
{
    const std::vector<Root>& args = login->getArgs();
    if (!args.front()->hasAttr("password") || !args.front()->hasAttr("username"))
        throw InvalidOperation("got bad LOGIN op");
    
    std::string username = args.front()->getAttr("username").asString();
    
    AccountMap::const_iterator A = m_server->findAccountByUsername(username);
    if (A  == m_server->m_accounts.end())
    {
        sendError("unknown account: " + username, login);
        return;
    }
    
    if (A->second->getPassword() != args.front()->getAttr("password").asString())
    {
        sendError("bad password", login);
        return;
    }
    
    // update the really important member variable
    m_account = A->second->getId();
    
    Info loginInfo;
    loginInfo->setArgs1(A->second);
    loginInfo->setTo(m_account);
    loginInfo->setRefno(login->getSerialno());
    send(loginInfo);
    
    m_server->joinRoom(m_account, "_lobby");
}

void ClientConnection::processAccountCreate(const Create& cr)
{

    // check for duplicate username

    AtlasPlayer acc;
    
    
    m_server->m_accounts[acc->getId()] = acc;
    
    Info createInfo;
    createInfo->setArgs1(acc);
    createInfo->setTo(m_account);
    createInfo->setRefno(cr->getSerialno());
    send(createInfo);
    
    m_server->joinRoom(m_account, "_lobby");
}

void ClientConnection::processOOGLook(const Look& lk)
{
    const std::vector<Root>& args = lk->getArgs();
    std::string lookTarget;
        
    if (args.empty())
    {
        debug() << "stub server got anonymous OOG look";
        lookTarget = "_lobby";
    }
    else
    {
        lookTarget = args.front()->getId();
        debug() << "stub server got OOG look at " << lookTarget;
    }
    
    RootEntity thing;
    if (m_server->m_accounts.count(lookTarget))
    {
        thing = m_server->m_accounts[lookTarget];
        if (lookTarget != lk->getFrom())
        {
            // prune
            thing->removeAttr("characters");
            thing->removeAttr("password");
        }
    }
    else if (m_server->m_world.count(lookTarget))
    {
        // ensure it's owned by the account, i.e in characters
        if (!entityIsCharacter(lookTarget))
        {
            sendError("not allowed to look at that entity", lk);
            return;
        }
        
        thing = m_server->m_world[lookTarget];
    }
    else if (m_server->m_rooms.count(lookTarget))
    {
        // should check room view permissions?
        thing = m_server->m_rooms[lookTarget];
    }
    else
    {
        // didn't find any entity with the id
        sendError("processed OOG look for unknown entity " + lookTarget, lk);
        return;
    }
    
    Sight st;
    st->setArgs1(thing);
    st->setFrom(lookTarget);
    st->setTo(lk->getFrom());
    st->setRefno(lk->getSerialno());
    send(st);
}

void ClientConnection::processAnonymousGet(const Get& get)
{
    const std::vector<Root>& args = get->getArgs();
    if (args.empty())
    {
        debug() << "handle server queries";
    } else {
        std::string typeName = args.front()->getId();
        debug() << "got a type query for '" << typeName << "', I think";
        if (m_server->m_types.count(typeName))
        {
            Info typeInfo;
            typeInfo->setArgs1(m_server->m_types[typeName]);
            typeInfo->setRefno(get->getSerialno());
            send(typeInfo);
        } else
            sendError("unknown type " + typeName, get);
    }
}

#pragma mark -

void ClientConnection::sendError(const std::string& msg, const RootOperation& op)
{
    Error errOp;
    
    errOp->setRefno(op->getSerialno());
    errOp->setTo(op->getFrom());
    
    Root arg0;
    arg0->setAttr("message", msg);
    
    std::vector<Root>& args = errOp->modifyArgs();
    args.push_back(arg0);
    args.push_back(op);
    
    send(errOp);
}

void ClientConnection::send(const Root& obj)
{
    m_encoder->streamObjectsMessage(obj);
    m_stream << std::flush;
}

bool ClientConnection::entityIsCharacter(const std::string& id)
{
    assert(!m_account.empty());
    AtlasPlayer p = m_server->m_accounts[m_account];
    StringSet characters(p->getCharacters().begin(),  p->getCharacters().end());
    
    return characters.count(id);
}