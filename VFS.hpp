/*
 * MIT License
 *
 * Copyright (c) 2020 Christian Tost
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef VFS_HPP
#define VFS_HPP

#include <string>
#include <chrono>
#include <vector>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <string.h>
#include <mutex>

namespace VFS
{
    #define CHUNK_SIZE 4096

    class CVFS;
    class CVFSNode;
    class CVFSFileStream;

    using VFSNode = std::shared_ptr<CVFSNode>;
    using VFSFileStream = std::shared_ptr<CVFSFileStream>;

    enum class VFSError
    {
        CANT_CREATE_DIR,
        CANT_CREATE_FILE,
        CANT_OPEN_FILE,
        OUT_OF_MEM,
        NODE_IS_FILE,
        NODE_IS_DIR,
        NODE_ALREADY_EXISTS,
        NODE_DOESNT_EXISTS
    };

    enum class FileMode
    {
        READ = 1,
        WRITE = 2,
        RW = (READ | WRITE),
        APPEND = 4
    };

    inline FileMode operator | (FileMode lhs, FileMode rhs)
    {
        return static_cast<FileMode>(static_cast<int>(lhs) | static_cast<int>(rhs));
    }

    inline FileMode& operator |= (FileMode& lhs, FileMode rhs)
    {
        lhs = lhs | rhs;
        return lhs;
    }

    inline FileMode operator & (FileMode lhs, FileMode rhs)
    {
        return static_cast<FileMode>(static_cast<int>(lhs) & static_cast<int>(rhs));
    }

    inline FileMode& operator &= (FileMode& lhs, FileMode rhs)
    {
        lhs = lhs & rhs;
        return lhs;
    }

    enum class Cursor
    {
        BEG,
        CUR,
        END
    };

    class CVFSException : public std::exception
    {
        public:
            CVFSException() {}
            CVFSException(VFSError Type) : m_ErrType(Type) {}
            CVFSException(const std::string &Msg, VFSError Type) : m_Msg(Msg), m_ErrType(Type) {}

            const char *what() const noexcept override
            {
                return m_Msg.c_str();
            }

            VFSError GetErrType() const noexcept
            {
                return m_ErrType;
            }

        private:
            std::string m_Msg;
            VFSError m_ErrType;
    };

    /**
     * @brief Base of all nodes.
     */
    class CVFSNode
    {
        friend CVFS;

        public:
            CVFSNode()
            {
                m_Created = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                m_Accessed = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            }

            CVFSNode(const CVFSNode &node)
            {
                m_Name = node.m_Name;
                m_IsDir = node.m_IsDir;

                m_Created = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                m_Accessed = node.m_Accessed;
            }

            /**
             * @return Gets the name of the node.
             */
            inline std::string Name() const
            {
                std::lock_guard<std::mutex> lock(m_UpdateLock);
                return m_Name;
            }

            /**
             * @return Returns true if this node is a dir.
             */
            inline bool IsDir() const
            {
                std::lock_guard<std::mutex> lock(m_UpdateLock);
                return m_IsDir;
            }

            /**
             * @return Returns the creation time of the node.
             */
            inline time_t Created() const
            {
                std::lock_guard<std::mutex> lock(m_UpdateLock);
                return m_Created;
            }
            
            /**
             * @return Returns the last access time of the node.
             */
            inline time_t Accessed() const
            {
                std::lock_guard<std::mutex> lock(m_UpdateLock);
                return m_Accessed;
            }

            /**
             * @return Returns a copy of this node.
             */
            virtual VFSNode Copy() = 0;

            virtual ~CVFSNode() = default;

        protected:
            std::string m_Name;
            bool m_IsDir;

            time_t m_Created;
            time_t m_Accessed;

            mutable std::mutex m_UpdateLock;
    };

    class CVFS
    {
        friend CVFSFileStream;

        public:
            CVFS(/* args */) 
            {
                //Creates the root node.
                m_Root = VFSDir(new CVFSDir("/"));
            }

            /**
             * @brief Create a new directory.
             * 
             * @param Path: Path to the directory.
             * @param Force: Creates all parent dirs, if they aren't exists.
             * 
             * @throw Throws a CVFSException, if the dir can't be created or the system is out of memory.
             */
            void CreateDir(const std::string &Path, bool Force = false)
            {
                auto Dirs = SplitPath(Path);
                auto CurDir = m_Root;

                for (size_t i = 0; i < Dirs.size(); i++)
                {
                    std::string Dir = Dirs[i];

                    auto node = CurDir->Search(Dir);

                    //Creates the directory either if Force is true or we are at the end of the path.
                    if(!node && (Force || (i == Dirs.size() - 1)))
                    {
                        VFSDir tmp;
                        try
                        {
                            tmp = VFSDir(new CVFSDir(Dir));
                            CurDir->AppendChild(tmp);
                        }
                        catch(const std::bad_alloc &e)
                        {
                            throw CVFSException("Can't create directory. Out of mem. bad_alloc: " + std::string(e.what()), VFSError::OUT_OF_MEM);
                        }
                        
                        CurDir = tmp;
                    }
                    else if(!node || !node->IsDir())
                        throw CVFSException("Can't create directory", VFSError::CANT_CREATE_DIR);
                    else
                        CurDir = std::static_pointer_cast<CVFSDir>(node);
                }               
            }

            /**
             * @return Gets the information of a given node. Returns null if the node wasn't found.
             */
            VFSNode GetNodeInfo(const std::string &Path)
            {
                auto Dirs = SplitPath(Path);
                auto CurDir = m_Root;
                VFSNode Ret;
                if(Path == "/")
                    Ret = m_Root;

                for (size_t i = 0; i < Dirs.size(); i++)
                {
                    std::string Dir = Dirs[i];

                    auto node = CurDir->Search(Dir);
                    if(node && (node->IsDir() || (i == Dirs.size() - 1)))   //"Go into" the directory. 
                    {
                        Ret = node;
                        CurDir = std::static_pointer_cast<CVFSDir>(node);
                    }
                    else
                    {
                        Ret = nullptr;
                        break;
                    }
                }

                return Ret;
            }

            /**
             * @return Checks if a given node already exists. Return true if the node exists.
             */
            bool NodeExists(const std::string &Path)
            {
                return GetNodeInfo(Path) != nullptr; 
            }

            /**
             * @return Gets a list of the content of a directory.
             * 
            * @throw Throws a CVFSException, if the given node is a file.
             */
            std::vector<VFSNode> List(const std::string &Path)
            {
                auto node = GetNodeInfo(Path);
                return List(node);
            }

            /**
             * @return Gets a list of the content of a directory.
             * 
             * @throw Throws a CVFSException, if the given node is a file.
             */
            std::vector<VFSNode> List(VFSNode node)
            {
                std::vector<VFSNode> Ret;
                if(node && node->IsDir())
                {
                    auto Dir = std::static_pointer_cast<CVFSDir>(node);
                    Ret = Dir->GetChilds();
                }
                else if(node && !node->IsDir())
                    throw CVFSException("Given node is not a directory", VFSError::NODE_IS_FILE);

                return Ret;
            }

            /**
             * @brief Creates or opens a file.
             * 
             * @param Path: Path to the file.
             * @param mode: Access mode.
             * 
             * @throw Throws a CVFSException, if the given node is a directory or if the file can't opened for readonly.
             */
            VFSFileStream Open(const std::string &Path, FileMode mode);

            /**
             * @return Returns the file size.
             */
            size_t FileSize(VFSNode node)
            {
                if(!node->IsDir())
                {
                    auto file = std::static_pointer_cast<CVFSFile>(node);
                    return file->Size();
                }
                else if(node && !node->IsDir())
                    throw CVFSException("Given node is not a file.", VFSError::NODE_IS_DIR);

                return 0;
            }

            /**
             * @brief Renames a node.
             * 
             * @param Path: Path to the node.
             * @param Name: New Name of the node.
             * 
             * @throw Throws a CVFSException, if a node with the given name already exists or the node doesn't exists.
             */
            void Rename(const std::string &Path, const std::string &Name)
            {
                if(NodeExists(Path))
                {
                    if(!NodeExists(ExtractPath(Path) + "/" + Name))
                    {
                        auto Parent = std::static_pointer_cast<CVFSDir>(GetNodeInfo(ExtractPath(Path)));
                        Parent->RenameChild(ExtractName(Path), Name);
                    }
                    else
                        throw CVFSException("Can't rename node. Node already exists.", VFSError::NODE_ALREADY_EXISTS);
                }
                else
                    throw CVFSException("Can't rename node. Node doesn't exists.", VFSError::NODE_DOESNT_EXISTS);
            }

            /**
             * @brief Moves a node.
             * 
             * @param From: The node to move.
             * @param To: Desitination of the node.
             * 
             * @throw Throws a CVFSException on error.
             */
            void Move(const std::string &From, const std::string &To)
            {
                if(!NodeExists(From))
                    throw CVFSException("Can't move node. Source node doesn't exists.", VFSError::NODE_DOESNT_EXISTS);

                if(!NodeExists(To))
                    throw CVFSException("Can't move node. Destination node doesn't exists.", VFSError::NODE_DOESNT_EXISTS);

                auto DestNode = GetNodeInfo(To);
                if(!DestNode->IsDir())
                    throw CVFSException("Can't move node. Destination node is a file.", VFSError::NODE_IS_FILE);

                auto node = GetNodeInfo(From);
                auto SrcParent = std::static_pointer_cast<CVFSDir>(GetNodeInfo(ExtractPath(From)));
                auto DestParent = std::static_pointer_cast<CVFSDir>(DestNode);

                SrcParent->RemoveChild(node->Name());
                DestParent->AppendChild(node);
            }

            /**
             * @brief Deletes a node.
             * 
             * @throw Throws a CVFSException on error.
             */
            void Delete(const std::string &Path)
            {
                if(!NodeExists(Path))
                    throw CVFSException("Can't delete node. Node doesn't exists.", VFSError::NODE_DOESNT_EXISTS);

                auto node = GetNodeInfo(Path);
                auto Parent = std::static_pointer_cast<CVFSDir>(GetNodeInfo(ExtractPath(Path)));

                Parent->RemoveChild(node->Name());
            }

            /**
             * @brief Copies a node.
             * 
             * @param From: The node to copy.
             * @param To: Desitination of the copy.
             * 
             * @throw Throws a CVFSException on error.
             */
            void Copy(const std::string &From, const std::string &To)
            {
                if(!NodeExists(From))
                    throw CVFSException("Can't copy node. Source node doesn't exists.", VFSError::NODE_DOESNT_EXISTS);

                if(NodeExists(To))
                    throw CVFSException("Can't copy node. Destination node already exists.", VFSError::NODE_ALREADY_EXISTS);

                auto DestNode = GetNodeInfo(ExtractPath(To));
                if(!DestNode->IsDir())
                    throw CVFSException("Can't copy node. Destination node parent is a file.", VFSError::NODE_IS_FILE);

                auto node = GetNodeInfo(From);
                auto DestParent = std::static_pointer_cast<CVFSDir>(DestNode);

                auto copy = node->Copy();
                copy->m_Name = ExtractName(To);

                DestParent->AppendChild(copy);
            }

            ~CVFS() {}
        private:
            class CVFSFile;
            class CVFSDir;

            using VFSFile = std::shared_ptr<CVFSFile>;
            using VFSDir = std::shared_ptr<CVFSDir>;
            
            class CVFSFile : public CVFSNode
            {
                public:
                    CVFSFile() : CVFSNode()
                    {
                        m_IsDir = false;
                        m_Modified = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                        m_Size = 0;
                    }

                    CVFSFile(const std::string &Name) : CVFSFile()
                    {
                        m_Name = Name;
                    }

                    CVFSFile(const CVFSFile &file) : CVFSNode(file)
                    {
                        m_Modified = file.m_Modified;
                        m_Size = file.m_Size;

                        ReserveChunks(file.m_Data.size());

                        //Copies the data content.
                        for (size_t i = 0; i < file.m_Data.size(); i++)
                        {
                            Chunk s = file.m_Data[i];
                            Chunk d = m_Data[i];

                            d->Filled = s->Filled;
                            memcpy(d->Data, s->Data, s->Filled);
                        }
                    }

                    /**
                     * @brief Clears the file.
                     */
                    void Clear()
                    {
                        std::lock_guard<std::mutex> lock(m_UpdateLock);
                        m_Data.clear();
                        ReserveChunks(4);
                    }

                    /**
                     * @brief Writes data to the file.
                     * 
                     * @param Data: Data to write.
                     * @param Size: Size of the data.
                     * 
                     * @return Returns the size which was written.
                     */
                    size_t Write(const char *Data, size_t Size)
                    {
                        std::lock_guard<std::mutex> lock(m_UpdateLock);

                        //Calculates the chunk count which is needed to save the data.
                        size_t ChunkCount = Size / CHUNK_SIZE + ((Size % CHUNK_SIZE > 0) ? 1 : 0);
                        if((m_Size == (m_Data.size() * CHUNK_SIZE)))    //Allocate new chunks, if we are exhausted.
                            ReserveChunks(ChunkCount);

                        size_t Written = 0;
                        size_t ChunkPos = m_Size / CHUNK_SIZE;  //Calculates the beginning chunk.

                        while (Written < Size)
                        {
                            Chunk c = m_Data[ChunkPos];
                            size_t Free = c->Size - c->Filled;
                            size_t CopyCount = (Size >= Free) ? Free : Size;    //Calculate the right copy size.

                            memcpy(c->Data + c->Filled, Data + Written, CopyCount);
                            c->Filled += CopyCount; //Chunk update
                            m_Size += CopyCount;  
                            Written += CopyCount;
                            ChunkPos++;
                        }

                        m_Modified = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                        return Written;
                    }

                    /**
                     * @brief Reads data from the file.
                     * 
                     * @param Buf: Buffer which receives the data.
                     * @param Size: Size of the buffer.
                     * @param CurPos: Position of inside the file.
                     * 
                     * @return Returns the size which was readed.
                     */
                    size_t Read(char *Buf, size_t Size, size_t CurPos)
                    {
                        std::lock_guard<std::mutex> lock(m_UpdateLock);

                        size_t ChunkPos = CurPos / CHUNK_SIZE; //Calculates the beginning chunk.
                        size_t Readed = 0;

                        while (Readed < Size)
                        {
                            if(ChunkPos >= m_Data.size())
                                break;

                            Chunk c = m_Data[ChunkPos];
                            size_t Pos = CurPos - ChunkPos * CHUNK_SIZE;
                            int CopyCount = c->Filled - Pos;
                            CopyCount = CopyCount > Size ? Size : CopyCount;    //Calculate the right copy size.
                            if(CopyCount <= 0)
                                break;

                            memcpy(Buf + Readed, c->Data + Pos, CopyCount);
                            Readed += CopyCount;
                            ChunkPos++;
                        }
                        
                        m_Accessed = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                        return Readed;
                    }

                    /**
                     * @return Returns the last modification time.
                     */
                    inline time_t Modified() const
                    {
                        std::lock_guard<std::mutex> lock(m_UpdateLock);
                        return m_Modified;
                    }


                    inline size_t Size() const
                    {
                        std::lock_guard<std::mutex> lock(m_UpdateLock);
                        return m_Size;
                    }

                    /**
                     * @return Returns a copy of this node.
                     */
                    VFSNode Copy() override
                    {
                        return VFSFile(new CVFSFile(*this));
                    }

                private:
                    /**
                     * Data chunk.
                     */
                    struct SChunk
                    {
                        public:
                            SChunk()
                            {
                                Size = CHUNK_SIZE;
                                Filled = 0;
                                Data = new char[Size];
                            }

                            int Size;
                            int Filled;
                            char *Data;

                            ~SChunk()
                            {
                                delete[] Data;
                            }
                    };

                    using Chunk = std::shared_ptr<SChunk>;

                    /**
                     * @brief Reserves new space for data.
                     * 
                     * @param Count: Chunks to allocate.
                     */
                    void ReserveChunks(size_t Count)
                    {
                        m_Data.reserve(Count);
                        for (size_t i = 0; i < Count; i++)
                            m_Data.push_back(Chunk(new SChunk()));
                    }

                    time_t m_Modified;
                    size_t m_Size;

                    std::vector<Chunk> m_Data;
            };

            class CVFSDir : public CVFSNode
            {
                public:
                    CVFSDir() : CVFSNode()
                    {
                        m_IsDir = true;
                    }

                    CVFSDir(const std::string &Name) : CVFSDir()
                    {
                        m_Name = Name;
                    }

                    CVFSDir(const CVFSDir &dir) : CVFSNode(dir)
                    {
                        m_Childs.reserve(dir.m_Childs.size());
                        for (auto &&e : dir.m_Childs)
                        {
                            m_Childs.push_back(e->Copy());
                        }
                    }

                    /**
                     * @brief Adds a new child to this directory.
                     * 
                     * @param Child: A file or dir to add.
                     */
                    void AppendChild(VFSNode Child)
                    {
                        std::lock_guard<std::mutex> lock(m_UpdateLock);
                        InternalAppendChild(Child);
                    }

                    /**
                     * @brief Searches for a node.
                     * 
                     * @param Name: Name of the node.
                     * 
                     * @return Returns the node of null if the node wasn't found.
                     */
                    VFSNode Search(const std::string &Name)
                    {
                        std::lock_guard<std::mutex> lock(m_UpdateLock);
                        VFSNode Ret;

                        if(!m_Childs.empty())
                        {
                            size_t Pos = Search(Name, 0, m_Childs.size() - 1);
                            if(m_Childs[Pos]->Name() == Name)
                                Ret = m_Childs[Pos];
                        }

                        return Ret;
                    }

                    /**
                     * @brief Renames and reorders a child.
                     * 
                     * @param Name: Current name of the child.
                     * @param NewName: New name of the child.
                     */
                    void RenameChild(const std::string &Name, const std::string &NewName)
                    {
                        std::lock_guard<std::mutex> lock(m_UpdateLock);
                        size_t Pos = Search(Name, 0, m_Childs.size() - 1);
                        auto Child = m_Childs[Pos];
                        if(Child->Name() == Name)
                        {
                            m_Childs.erase(m_Childs.begin() + Pos); //Removes the child temporary.
                            Child->m_Name = NewName;

                            InternalAppendChild(Child);
                        }
                    }

                    /**
                     * @brief Removes a child.
                     * 
                     * @param Name: Name of the child.
                     */
                    void RemoveChild(const std::string &Name)
                    {
                        std::lock_guard<std::mutex> lock(m_UpdateLock);
                        size_t Pos = Search(Name, 0, m_Childs.size() - 1);
                        auto Child = m_Childs[Pos];
                        if(Child->Name() == Name)
                            m_Childs.erase(m_Childs.begin() + Pos); //Removes the child.
                    }

                    /**
                     * @return Returns all childs of this dir.
                     */
                    std::vector<VFSNode> GetChilds()
                    {
                        std::lock_guard<std::mutex> lock(m_UpdateLock);
                        m_Accessed = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                        return m_Childs;
                    }

                    /**
                     * @return Returns a copy of this node.
                     */
                    VFSNode Copy() override
                    {
                        return VFSDir(new CVFSDir(*this));
                    }

                private:
                    /**
                     * @brief Binary search for faster searches.
                     * 
                     * @param Name: Name of the node to find.
                     * @param Start: Start of the frame.
                     * @param End: End of the frame.
                     * 
                     * @return Returns the position of the node.
                     */
                    size_t Search(const std::string &Name, size_t Start, size_t End)
                    {
                        if(End < Start || End == (size_t) -1)
                            return 0;
                        else if(End == Start)
                            return End;

                        size_t Middle = Start + (End - Start) / 2;
                        std::string PosName = m_Childs[Middle]->Name();
                        if(PosName == Name)
                            return Middle;
                        else if(Name > PosName)
                            return Search(Name, Middle + 1, End);
                        else if(Name < PosName)
                            return Search(Name, Start, Middle - 1);

                        return Middle;                        
                    }

                    /**
                     * @brief Adds a new child to this directory.
                     * 
                     * @param Child: A file or dir to add.
                     */
                    void InternalAppendChild(VFSNode Child)
                    {
                        size_t Pos = 0;

                        //Sorts the data ascending.
                        for (Pos = 0; Pos < m_Childs.size(); Pos++)
                        {
                            if(Child->Name() < m_Childs[Pos]->Name())
                                break;
                        }
                        
                        m_Childs.insert(m_Childs.begin() + Pos, Child);
                    }

                    std::vector<VFSNode> m_Childs;
            };

            /**
             * @brief Splits into a list of names.
             * 
             * @param Path: Path to split.
             * 
             * @return Returns the path as list.
             */
            std::vector<std::string> SplitPath(const std::string &Path)
            {
                size_t Pos = 0;
                std::vector<std::string> Ret;

                //Ignores the first '/' if it exitst.
                if(!Path.empty() && Path[0] == '/')
                    Pos++;

                while (Pos != std::string::npos)
                {
                    size_t Start = Pos;
                    Pos = Path.find('/', Start);

                    std::string Dir = Path.substr(Start, Pos - Start);
                    
                    if(!Dir.empty())
                        Ret.push_back(Dir);

                    if(Pos != std::string::npos)
                        Pos++;
                }
                
                return Ret;
            }

            /**
             * @return Returns a path without the last child e.g Path: /test/test.txt -> ret: /test
             */
            std::string ExtractPath(const std::string &Path)
            {
                size_t Pos = Path.find_last_of("/");
                if(Pos == Path.length() - 1)
                    Pos = Path.find_last_of("/", Pos - 1);

                return Path.substr(0, Pos);
            }

            /**
             * @return Returns the name of the last child
             */
            std::string ExtractName(const std::string &Path)
            {
                size_t End = std::string::npos;
                size_t Pos = Path.find_last_of("/");
                if(Pos == Path.length() - 1)
                {
                    End = Pos - 1;
                    Pos = Path.find_last_of("/", Pos - 1);
                }

                return Path.substr(Pos + 1, End - Pos);
            }
            
            VFSDir m_Root;
    };

    /**
     * @brief File reader and writer.
     */
    class CVFSFileStream
    {
        public:
            CVFSFileStream(CVFS::VFSFile file, FileMode mode) : m_File(file), m_Mode(mode), m_CurPos(0)
            {
                if((mode & FileMode::APPEND) != FileMode::APPEND)
                    m_File->Clear();
            }

            /**
             * @brief Writes a line to the file.
             * 
             * @param Line: Line to write.
             * 
             * @return Returns the size of written bytes.
             */
            size_t WriteLine(const std::string &Line)
            {
                char c = '\n';
                size_t Ret = Write(Line.data(), Line.size());
                Ret += Write(&c, sizeof(c));

                return Ret;
            }

            /**
             * @brief Writes a string to the file.
             * 
             * @param Str: String to write.
             * 
             * @return Returns the size of written bytes.
             */
            size_t Write(const std::string &Str)
            {
                return Write(Str.data(), Str.size());
            }

            /**
             * @brief Writes data to the file.
             * 
             * @param Data: Data to be written
             * @param Size: Data Size;
             * 
             * @return Returns the size of written bytes.
             */
            inline size_t Write(const char *Data, size_t Size)
            {
                if((m_Mode & FileMode::WRITE) == FileMode::WRITE)
                {
                    return m_File->Write(Data, Size);
                }

                return 0;
            }

            /**
             * @brief Reads a line of the file.
             * 
             * @return Returns the Line.
             */
            std::string ReadLine()
            {
                std::string Ret;
                char c;
                while (Read(&c, sizeof(c)) != 0)
                {
                    if(c == '\n')
                        break;

                    Ret += c;
                }

                return Ret;
            }

            /**
             * @brief Reads the whole file into a string
             * 
             * @return Returns the file content as string.
             */
            std::string Read()
            {
                std::string Ret;
                Ret.resize(Size());
                Read(&Ret[0], Ret.size());

                return Ret;
            }

            /**
             * @brief Reads data of the file.
             * 
             * @param Buf: Buffer to fill.
             * @param Size: Buffer Size;
             * 
             * @return Returns the size of readed bytes.
             */
            inline size_t Read(char *Buf, size_t Size)
            {
                if(((m_Mode & FileMode::READ) == FileMode::READ) && this->Size() != 0)
                {
                    size_t Ret = m_File->Read(Buf, Size, m_CurPos);
                    m_CurPos += Ret;
                    return Ret;
                }

                return 0;
            }

            /**
             * @brief Sets the cursor position inside the file.
             * 
             * @param cur: Either BEG, CUR or END
             * @param Bytes: Bytes to seek.
             */
            inline void Seek(Cursor cur, int64_t Bytes)
            {
                if(Size() == 0)
                    return;

                switch (cur)
                {
                    case Cursor::BEG:
                    {
                        m_CurPos = (size_t)Bytes > Size() ? Size() : Bytes;
                    }break;

                    case Cursor::CUR:
                    {
                        size_t NewPos = m_CurPos + Bytes;
                        m_CurPos = NewPos > Size() ? Size() : NewPos;
                    }break;

                    case Cursor::END:
                    {
                        size_t NewPos = Size() + Bytes;
                        m_CurPos = NewPos > Size() ? Size() : NewPos;
                    }break;
                }
            }

            /**
             * @return Returns the current cursor position inside the file.
             */
            inline size_t Tell() const
            {
                return m_CurPos;
            }

            /**
             * @return Returns the file size.
             */
            inline size_t Size() const
            {
                return m_File->Size();
            }

            /**
             * @return Returns true if the end of the file is reached.
             */
            inline bool IsEOF() const
            {
                return m_CurPos >= Size();
            }

            /**
             * @return Returns the file name.
             */
            inline std::string Name() const
            {
                return m_File->Name();
            }

            virtual ~CVFSFileStream() {}
        private:
            CVFS::VFSFile m_File;
            FileMode m_Mode;

            size_t m_CurPos;
    };

    inline VFSFileStream CVFS::Open(const std::string &Path, FileMode mode)
    {
        VFSFileStream ret;
        auto node = GetNodeInfo(Path);
        if(node && !node->IsDir())
            ret = VFSFileStream(new CVFSFileStream(std::static_pointer_cast<CVFSFile>(node), mode));
        else if(node && node->IsDir())
            throw CVFSException("Can't open file. A directory with the given name already exists.", VFSError::CANT_CREATE_FILE);
        else if((mode & FileMode::WRITE) == FileMode::WRITE)    //Creates a new file.
        {
            node = GetNodeInfo(ExtractPath(Path));
            if(node)
            {
                auto file = VFSFile(new CVFSFile(ExtractName(Path)));
                auto dir = std::static_pointer_cast<CVFSDir>(node);
                dir->AppendChild(file);
                ret = VFSFileStream(new CVFSFileStream(file, mode));
            }
        }
        else
            throw CVFSException("Can't open file. File doesn't exists.", VFSError::CANT_OPEN_FILE);

        return ret;
    }
} // namespace VFS


#endif //VFS_HPP