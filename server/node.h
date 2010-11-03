
/* Copyright (c) 2005-2010, Stefan Eilemann <eile@equalizergraphics.com> 
 *                    2010, Cedric Stalder <cedric Stalder@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *  
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef EQSERVER_NODE_H
#define EQSERVER_NODE_H

#include "config.h"                // used in inline method
#include "connectionDescription.h" // used in inline method
#include "state.h"                 // enum
#include "types.h"

#include <eq/fabric/node.h> // base class

#include <eq/net/barrier.h>
#include <eq/net/bufferConnection.h>
#include <eq/net/node.h>

#include <vector>

namespace eq
{
namespace server
{
    /** The node. */
    class Node : public fabric::Node< Config, Node, Pipe, NodeVisitor >
    {
    public:
        /** 
         * Constructs a new Node.
         */
        EQSERVER_EXPORT Node( Config* parent );

        virtual ~Node();

        /** @name Data Access. */
        //@{
        ServerPtr getServer();
        ConstServerPtr getServer() const;

        net::NodePtr getNode() const { return _node; }
        void setNode( net::NodePtr node ) { _node = node; }

        Channel* getChannel( const ChannelPath& path );

        /** @return the state of this node. */
        State getState()    const { return _state.get(); }

        /** @internal */
        void setState( const State state ) { _state = state; }

        net::CommandQueue* getMainThreadQueue();
        net::CommandQueue* getCommandThreadQueue();

        /** Increase node activition count. */
        void activate();

        /** Decrease node activition count. */
        void deactivate();

        /** @return if this pipe is actively used for rendering. */
        bool isActive() const { return ( _active != 0 ); }

        /** @return if this node is running. */
        bool isRunning() const { return _state == STATE_RUNNING; }

        /** @return if this node is stopped. */
        bool isStopped() const { return _state == STATE_STOPPED; }

        /**
         * Add additional tasks this pipe, and all its parents, might
         * potentially execute.
         */
        void addTasks( const uint32_t tasks );

        /** The last drawing channel for this entity. */
        void setLastDrawPipe( const Pipe* pipe )
            { _lastDrawPipe = pipe; }
        const Pipe* getLastDrawPipe() const { return _lastDrawPipe;}

        /** @return the number of the last finished frame. @internal */
        uint32_t getFinishedFrame() const { return _finishedFrame; }
        //@}

        /**
         * @name Operations
         */
        //@{
        /** Connect the render slave node process. */
        bool connect();

        /** Launch the render slave node process. */
        bool launch();

        /** Synchronize the connection of a render slave launch. */
        bool syncLaunch( const base::Clock& time );

        /** Start initializing this entity. */
        void configInit( const uint32_t initID, const uint32_t frameNumber );

        /** Sync initialization of this entity. */
        bool syncConfigInit();

        /** Start exiting this entity. */
        void configExit();

        /** Sync exit of this entity. */
        bool syncConfigExit();

        /** 
         * Trigger the rendering of a new frame for this node.
         *
         * @param frameID a per-frame identifier passed to all rendering
         *                methods.
         * @param frameNumber the number of the frame.
         */
        void update( const uint32_t frameID, const uint32_t frameNumber );

        /** 
         * Flush the processing of frames, including frameNumber.
         *
         * @param frameNumber the number of the frame.
         */
        void flushFrames( const uint32_t frameNumber );

        /** Synchronize the completion of the rendering of a frame. */
        void finishFrame( const uint32_t frame );
        //@}


        /**
         * @name Barrier Cache
         *
         * Caches barriers for which this node is the master.
         */
        //@{
        /** 
         * Get a new barrier of height 0.
         * 
         * @return the barrier.
         */
        net::Barrier* getBarrier();

        /** 
         * Release a barrier server by this node.
         * 
         * @param barrier the barrier.
         */
        void releaseBarrier( net::Barrier* barrier );

        /** Change the latency on all objects (barrier) */
        void changeLatency( const uint32_t latency );
        //@}

        void send( net::SessionPacket& packet ) 
            { 
                packet.sessionID = getConfig()->getID(); 
                _bufferedTasks.send( packet );
            }
        void send( net::SessionPacket& packet, const std::string& string ) 
            {
                packet.sessionID = getConfig()->getID(); 
                _bufferedTasks.send( packet, string );
            }
        template< typename T >
        void send( net::SessionPacket &packet, const std::vector<T>& data )
            {
                packet.sessionID = getConfig()->getID(); 
                _bufferedTasks.send( packet, data );
            }

        void flushSendBuffer();

        /** 
         * Add a new description how this node can be reached.
         * 
         * @param desc the connection description.
         */
        void addConnectionDescription( ConnectionDescriptionPtr desc )
            { _connectionDescriptions.push_back( desc ); }
        
        /** 
         * Remove a connection description.
         * 
         * @param cd the connection description.
         * @return true if the connection description was removed, false
         *         otherwise.
         */
        EQSERVER_EXPORT bool removeConnectionDescription(
            ConnectionDescriptionPtr cd );

        /** @return the vector of connection descriptions. */
        const ConnectionDescriptions& getConnectionDescriptions()
            const { return _connectionDescriptions; }


        /** @name Attributes */
        //@{
        /** String attributes. */
        enum SAttribute
        {
            SATTR_LAUNCH_COMMAND, //!< the command to launch the node
            SATTR_LAST,
            SATTR_ALL = SATTR_LAST + 5
        };

        /** Character attributes. */
        enum CAttribute
        {
            CATTR_LAUNCH_COMMAND_QUOTE, //!< The character to quote arguments
            CATTR_LAST,
            CATTR_ALL = CATTR_LAST + 5
        };

        /** @internal Set a string integer attribute. */
        void setSAttribute( const SAttribute attr, const std::string& value );

        /** @internal Set a character integer attribute. */
        void setCAttribute( const CAttribute attr, const char value );

        /** @return the value of a node string attribute. @version 1.0 */
        const std::string& getSAttribute( const SAttribute attr ) const;

        /** @return the value of a node string attribute. @version 1.0 */
        char getCAttribute( const CAttribute attr ) const;

        /** @internal @return the name of a node string attribute. */
        static const std::string& getSAttributeString( const SAttribute attr );
        /** @internal @return the name of a node character attribute. */
        static const std::string& getCAttributeString( const CAttribute attr );
        //@}

        void output( std::ostream& os ) const; //!< @internal

    protected:

        /** @sa net::Object::attachToSession. */
        virtual void attachToSession( const uint32_t id, 
                                      const uint32_t instanceID, 
                                      net::Session* session );
        
        void deserialize( net::DataIStream& is, const uint64_t dirtyBits );
    
    private:
        /** String attributes. */
        std::string _sAttributes[SATTR_ALL];

        /** Character attributes. */
        char _cAttributes[CATTR_ALL];

        /** Number of activations for this node. */
        uint32_t _active;

        /** The network node on which this Equalizer node is running. */
        net::NodePtr _node;

        /** The list of descriptions on how this node is reachable. */
        ConnectionDescriptions _connectionDescriptions;

        /** The frame identifiers non-finished frames. */
        std::map< uint32_t, uint32_t > _frameIDs;

        /** The number of the last finished frame. */
        uint32_t _finishedFrame;

        /** The number of the last flushed frame (frame finish packet sent). */
        uint32_t _flushedFrame;

        /** The current state for state change synchronization. */
        base::Monitor< State > _state;
            
        /** The cached barriers. */
        std::vector<net::Barrier*> _barriers;

        /** Task packets for the current operation. */
        net::BufferConnection _bufferedTasks;

        /** The last draw pipe for this entity */
        const Pipe* _lastDrawPipe;

        union // placeholder for binary-compatible changes
        {
            char dummy[32];
        };

        /** 
         * Compose the launch command by expanding the variables in the
         * launch command string.
         *
         * @param description the connection description.
         * @return the expanded launch command.
         */
        std::string _createLaunchCommand( ConnectionDescriptionPtr description);
        std::string   _createRemoteCommand();

        uint32_t _getFinishLatency() const;
        void _finish( const uint32_t currentFrame );

        /** flush cached barriers. */
        void _flushBarriers();

        void _send( net::ObjectPacket& packet ) 
            { packet.objectID = getID(); send( packet ); }
        void _send( net::ObjectPacket& packet, const std::string& string ) 
            { packet.objectID = getID(); send( packet, string ); }

        /** Send the frame finish packet for the given frame number. */
        void _sendFrameFinish( const uint32_t frameNumber );

        /* Command handler functions. */
        bool _cmdConfigInitReply( net::Command& command );
        bool _cmdConfigExitReply( net::Command& command );
        bool _cmdFrameFinishReply( net::Command& command );
    };
}
}
#endif // EQSERVER_NODE_H
