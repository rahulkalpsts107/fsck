/*rrk310*/
#include<iostream>
#include<fstream>
#include<string>
#include<cstring>
#include<map>
#include<cstdlib>
#include<vector>
#include<sstream>
#include<algorithm>
#include<ctime>
#include<cstdio>
#include<bitset>

using namespace std;
                
/* Errors*/
#define SUCCESS 0
#define ERR_WRITE_FAIL 1<<0
#define ERR_SUP_OPEN_FAIL 1<<1
#define ERR_UNKNOWN_PARSE_ELEM 1<<2
#define ERR_INVALID_SUP_BLK 1<<3

/* Constants*/
#define BLOCK_SZ 4096
#define FILE_SYSTEM_PRE "fusedata."
#define SUPER_BLOCK_LOC 0
#define MAX_POINTERS 400
#define DEV_ID  20
#define MAX_BLOCKS 10000
#define MARK_OCCUPIED(blk,flag) blockBitmap.set(blk,flag)

static const string test_loc =  "F:\\fusedata\\";
static string stage;
static const string TAG = " csefsck ";

/*prototype*/
string retAbsPath(string loc,string pre,string bl_no);
enum InodeType
{
    INODE_SUPER,
    INODE_DIR,
    INODE_FILE,
};

bitset<MAX_BLOCKS> blockBitmap;

/*Classes*/
class AbstractNode
{
private:
    friend std::ostream & operator<<(std::ostream &os, const AbstractNode* node);
    InodeType type;
protected:
    AbstractNode(InodeType itype):type(itype){}
public:
    virtual int     setField(const char *key,const char *val,const char *extra)=0;
    virtual int     parse(string loc)=0;
    virtual string  toString()=0;
    virtual int     chkAndRepair()=0;
    virtual void    save()=0;
    virtual int     getType(){return type;};
};

std::ostream & operator<<(std::ostream &os, AbstractNode* node)
{
    return os << node->toString().c_str();
}

int innerParse(string entry,AbstractNode *thiz)
{
    unsigned first = entry.find("{");
    unsigned end = entry.find("}");
    entry= entry.substr (first+1,end-first-1);
    stringstream lineStream(entry);
    string temp;
    int ret = SUCCESS;
    while(std::getline(lineStream,temp,','))
    {
        std::string delimiter = ":";
        int positionOfEquals = temp.find(delimiter);
        char type[2];
        strncpy(&type[0],temp.data(),1);
        char nkey[255];
        char nval[255];
        if(positionOfEquals != string::npos)
        {
            string kv = temp.substr(positionOfEquals + 1).c_str();
            positionOfEquals = kv.find(delimiter);
            const char *key = kv.substr(0, positionOfEquals ).c_str();
            const char *value = NULL;
            strcpy(&nkey[0],key);
            if(positionOfEquals != string::npos)
            {
              value = kv.substr(positionOfEquals + 1).c_str();
            }
            strcpy(&nval[0],value);
        }
        else
        {
            ret = ERR_UNKNOWN_PARSE_ELEM;
            break;
        }
        ret = thiz->setField(&nkey[0],&nval[0],type);
    }
    return ret;
}

int csvParse(string entry,AbstractNode *thiz)
{
    unsigned first = entry.find("{");
    unsigned end = entry.find("}");
    entry= entry.substr (first+1,end-first-1);
    int found = entry.find("location");
    if (found != string::npos)
    {
        if(entry[found-1]!=',')
        entry.insert(found,",");
    }
    stringstream lineStream(entry);
    string temp;
    int ret = SUCCESS;
    while(std::getline(lineStream,temp,','))
    {
        std::string delimiter = ":";
        int positionOfEquals = temp.find(delimiter);
        const char *key = temp.substr(0, positionOfEquals ).c_str();
        const char *value = NULL;
        char nkey[255];
        char nval[255];
        strcpy(&nkey[0],key);
        if(positionOfEquals != string::npos)
        {
          value = temp.substr(positionOfEquals + 1).c_str();
        }
        strcpy(&nval[0],value);
        if(value)
        {
            if(value[0]=='{')
            {
                ret = innerParse(entry,thiz);
                break;
            }
            else
                ret = thiz->setField(&nkey[0],&nval[0],NULL);
        }
        else 
            ret = ERR_UNKNOWN_PARSE_ELEM;
    }
    return ret;
}
  
int fileWrite(string fileLoc,unsigned int inoNum,string tostring)
{
    int ret = SUCCESS;
    std::ostringstream o;
    o<<inoNum;
    string temp = fileLoc+string(FILE_SYSTEM_PRE)+string("tmp")+o.str();
    ofstream super(temp.c_str());
    if(super.is_open())
    {
        super<<tostring;
        string orig = fileLoc+string(FILE_SYSTEM_PRE)+o.str();
        remove(orig.c_str());
        super.close();
        rename(temp.c_str(),orig.c_str());
    }
    return ret;
}

int readBlock(int location,string fileLoc,string &entry)
{
    int ret = SUCCESS;
    std::ostringstream o;
    o<<location;
    string dir = retAbsPath(fileLoc,FILE_SYSTEM_PRE,o.str()); 
    ifstream fileBlk(dir.c_str());
    if(fileBlk.is_open())
    {
        getline(fileBlk,entry);
        fileBlk.close();
        entry.erase( ::remove_if( entry.begin(), entry.end(), ::isspace ), entry.end() );
    }
    else
    {
        ret = ERR_SUP_OPEN_FAIL;
    }
    return ret;
}
class SuperBlock: public AbstractNode
{
public:
    /*Members*/
    unsigned long    creatTime;
    int             timesMounted;
    int                 devId;
    unsigned int     freeStart;
    unsigned int     freeEnd;
    unsigned int     rootBlk;
    unsigned int     maxBlocks;
    bool            isDirty;    
    string          fileLoc;
    unsigned int     inoNum;
    int getRoot()
    {
        return rootBlk;
    }

    SuperBlock(int num):AbstractNode(INODE_SUPER),inoNum(num)
    {
        isDirty = false;
    }
    virtual int setField(const char *key,const char *val,const char *extra)
    {
        int ret = SUCCESS;
        if(strcmp(key,"creationTime") == 0)      creatTime = atol(val);
        else if(strcmp(key,"mounted")== 0)      timesMounted = atoi(val);
        else if(strcmp(key,"devId")== 0)        devId = atoi(val);
        else if(strcmp(key,"freeStart")== 0)    freeStart = atoi(val);
        else if(strcmp(key,"freeEnd")== 0)      freeEnd = atoi(val);
        else if(strcmp(key,"root")== 0)         rootBlk = atoi(val);
        else if(strcmp(key,"maxBlocks")== 0)    maxBlocks = atoi(val);
        else 
        {
            ret = ERR_UNKNOWN_PARSE_ELEM;
        }
        return ret;
    }
    virtual int parse(string loc)
    {
        int ret = SUCCESS;   
        string entry;
        fileLoc = loc;
        ret = readBlock(inoNum,loc,entry);
        ret = csvParse(entry,this);
        return ret;
    }
    
    string toString()
    {
        std::ostringstream o;
        o<<"{creationTime: "<<creatTime<<", mounted: "<<timesMounted<<", devId: "<<devId<<", freeStart: " \
        <<freeStart<<", freeEnd: "<<freeEnd<<", root: "<<rootBlk<<", maxBlocks: "<<maxBlocks<<"}"<<endl;
        return o.str();
    }
    
    int chkAndRepair()
    {
        int ret = SUCCESS;
        if (devId != DEV_ID) 
        {
            cout<<TAG<<stage<<"Sup blk:Unknown FS!"<<endl;
            ret = ERR_INVALID_SUP_BLK;
        }
        else
        {
            int max_blk = MAX_BLOCKS;
            unsigned int bl_no;
            for(bl_no =freeStart;bl_no<=freeEnd;bl_no++)
                MARK_OCCUPIED(bl_no,true);
            if(maxBlocks>max_blk)
            {
                cout<<TAG<<stage<<"Sup blk:Max block exceeded "<<endl;
                maxBlocks = max_blk;
                isDirty = true;
            }
            int max_blk_node = freeEnd - freeStart;
            if (max_blk_node >max_blk)
            {
                cout<<TAG<<stage<<"Sup blk:Invalid free blocks, exceeds max blocks in fs"<<endl;
                freeEnd = freeStart+max_blk;
                isDirty = true;
            }
            
            time_t cur = std::time(NULL);
            if(creatTime>cur || creatTime <=0 )
            {
                cout<<TAG<<stage<<"Sup blk:Invalid creat time"<<endl;
                creatTime = cur;
                isDirty = true;
            }
        }
        save();
        return ret;
    }
    
    virtual void save()
    {
        if(isDirty == true)
        {
            int ret = SUCCESS;
            cout<<TAG<<stage<<"Flush changes to disk for super block"<<endl;
            cout<<TAG<<stage<<this->toString()<<endl;
            ret = fileWrite(fileLoc,inoNum,this->toString());
            if (ret == SUCCESS)
                isDirty = false;
        }
    }
};

class DirBlock:public AbstractNode
{
public:
    int size;
    int uid;
    int gid;
    int mode;
    unsigned long atime;
    unsigned long ctime;
    unsigned long mtime;
    int linkCount;
    map<string,int> dirs;
    map<string,int> files;
    bool isDirty;
    string fileLoc;
    int cur;
    int prev;
    unsigned int inoNum;
    int isRoot;
    
    DirBlock(int num):AbstractNode(INODE_DIR),inoNum(num)
    {
        isDirty = false;
        cur=-1;
        prev=-1;
        isRoot=0;
    }
    
    virtual int setField(const char *key,const char *val,const char *extra)
    {
        int ret = SUCCESS;
        if(strcmp(key,"size") == 0)             size = atol(val);
        else if(strcmp(key,"uid")== 0)          uid = atoi(val);
        else if(strcmp(key,"gid")== 0)          gid = atoi(val);
        else if(strcmp(key,"mode")== 0)         mode = atoi(val);
        else if(strcmp(key,"atime")== 0)        atime = atoi(val);
        else if(strcmp(key,"ctime")== 0)        ctime = atoi(val);
        else if(strcmp(key,"mtime")== 0)        mtime = atoi(val);
        else if(strcmp(key,"linkcount")== 0)    linkCount = atoi(val);   
        else if(strcmp(key,".")== 0)            cur = atoi(val);   
        else if(strcmp(key,"..")== 0)           prev = atoi(val);   
        else
        {
            if(extra != NULL)
            {
                char nval[255];
                strcpy(nval,val);
                if(strcmp(extra,"f")==0)
                    files[key] = atoi(nval);
                else if(strcmp(extra,"d")==0)
                    dirs[key] = atoi(nval);
                else
                    ret = ERR_UNKNOWN_PARSE_ELEM;
            }
            else 
            {
                ret = ERR_UNKNOWN_PARSE_ELEM;
            }
        }
        
        return ret;
    }
    virtual int parse(string loc)
    {
        int ret = SUCCESS;   
        fileLoc = loc;
        string entry;
        ret = readBlock(inoNum,fileLoc,entry);
        ret = csvParse(entry,this);
        return ret;
    }
    virtual string toString()
    {
        std::ostringstream o;
        o<<"{size: "<<size<<", uid: "<<uid<<", gid: "<<gid<<", mode: " \
        <<mode<<", atime: "<<atime<<", ctime: "<<ctime<<", mtime: "<<mtime<<",linkcount: "<<linkCount<<", filename_to_inode_dict: {";
        typedef map<string,int>::iterator it_type;
        o<<"d:.:"<<cur<<", d:..:"<<prev;
        for(it_type it = dirs.begin(); it != dirs.end(); it++) {
            o<<", d:"<<it->first;
            o<<":"<<it->second;
        }
        int size = files.size();
        for(it_type it = files.begin(); it != files.end(); it++) {
            o<<", f:"<<it->first;
            o<<":"<<it->second;
            if (size > 1)
                o<<", ";
            --size;
        }
        o<<"}}"<<endl;
        return o.str();
    }
   
    virtual int chkAndRepair()
    {
        int ret = SUCCESS;
        int lc = dirs.size()+files.size()+2;
        time_t currentTime = std::time(NULL);
        if(ctime>currentTime || ctime <=0)
        {
            cout<<TAG<<stage<<"Dir blk:Invalid creat time"<<endl;
            ctime = currentTime;
            atime = currentTime;
            mtime = currentTime;
            isDirty = true;
        }
        else if (atime>currentTime){cout<<TAG<<stage<<"Dir blk:Invalid access time "<<endl;atime = currentTime;isDirty = true;}
        else if (mtime>currentTime){cout<<TAG<<stage<<"Dir blk:Invalid modify time "<<endl;mtime = currentTime;isDirty = true;}
        if (linkCount != lc)
        {
            cout<<TAG<<stage<<"Invalid link count"<<endl;
            linkCount = lc;
            isDirty = true;
        }
        if (isRoot)
        {
            if((cur != inoNum) || (prev != inoNum))
            {
                cout<<TAG<<stage<<"Dir blk: Correct current and prev dir "<<endl;
                cur = inoNum;
                prev = cur;
                isDirty = true;
            }
        }
        else
        {
            if (cur != inoNum)
            {
                cout<<TAG<<stage<<"Dir blk: Correct current dir "<<endl;
                cur = inoNum;
                isDirty = true;
            }
        }
        save();
        return ret;
    }
    
    virtual void save()
    {
        if(isDirty == true)
        {
            int ret = SUCCESS;
            cout<<TAG<<stage<<"Flush changes to disk for dir block - "<<inoNum<<endl;
            cout<<TAG<<stage<<this->toString()<<endl;
            ret = fileWrite(fileLoc,inoNum,this->toString());
            if (ret == SUCCESS)
                isDirty = false;
        }
    }
    
    void checkPrevAndCur(DirBlock *prev)
    {
        if (prev!=NULL)
        if (prev->cur != this->prev)
        {
            cout<<TAG<<stage<<"Dir blk: Correct prev dir "<<endl;
            this->prev = prev->cur;
            isDirty = true;
        }
        save();
    }
};

class FileBlock:public AbstractNode
{
public:
    int size;
    int uid;
    int gid;
    int mode;
    unsigned long atime;
    unsigned long ctime;
    unsigned long mtime;
    int linkCount;
    int indirect;
    unsigned int location;
    bool isDirty;
    string fileLoc;
    unsigned int inoNum;
    
    FileBlock(int num):AbstractNode(INODE_FILE)
    {
        inoNum = num;
        isDirty = false;
    }
    
    virtual string toString()
    {
        std::ostringstream o;
        o<<"{size: "<<size<<", uid: "<<uid<<", gid: "<<gid<<", mode: " \
        <<mode<<", atime: "<<atime<<", ctime: "<<ctime<<", mtime: "<<mtime<<", linkcount: "<<linkCount \
        <<", indirect: "<<indirect<<", location: "<<location<<"}";
        return o.str();
    }
    
    virtual int setField(const char *key,const char *val,const char *extra)
    {
        int ret = SUCCESS;
        if(strcmp(key,"size") == 0)             size = atol(val);
        else if(strcmp(key,"uid")== 0)          uid = atoi(val);
        else if(strcmp(key,"gid")== 0)          gid = atoi(val);
        else if(strcmp(key,"mode")== 0)         mode = atoi(val);
        else if(strcmp(key,"linkcount")== 0)    linkCount = atoi(val);
        else if(strcmp(key,"atime")== 0)        atime = atoi(val);
        else if(strcmp(key,"ctime")== 0)        ctime = atoi(val);
        else if(strcmp(key,"mtime")== 0)        mtime = atoi(val);   
        else if(strcmp(key,"indirect")== 0)     indirect = atoi(val);
        else if(strcmp(key,"location")== 0)     location = atoi(val);
        else 
        {
            ret = ERR_UNKNOWN_PARSE_ELEM;
        }
        return ret;
    }
    
    virtual void save()
    {
        if(isDirty == true)
        {
            int ret = SUCCESS;
            cout<<TAG<<stage<<"Flush changes to disk for file block - "<<inoNum<<endl;
            cout<<TAG<<stage<<this->toString()<<endl;
            ret = fileWrite(fileLoc,inoNum,this->toString());
            
            if (ret != SUCCESS)
                cout<<TAG<<stage<<"File write failed for inode "<<inoNum<<endl;
            isDirty = false;
        }
    }
    
    virtual int chkAndRepair()
    {
        int ret = SUCCESS;
        vector<string>blocks;
        string entry;
        if(location == inoNum) cout<<TAG<<stage<<"<FATAL> detected a self referencing dir block "<<inoNum<<endl;
        ret = readBlock(location,fileLoc,entry);
        int isIndirect = chkIndirBlks(entry,blocks);
        MARK_OCCUPIED(location,true);
        int numBlks = blocks.size();
        time_t cur = std::time(NULL);
        if(ctime>cur || ctime <=0)
        {
            cout<<TAG<<stage<<"file blk:Invalid creat time"<<endl;
            ctime = cur;
            atime = cur;
            mtime = cur;
            isDirty = true;
        }
        else if (atime>cur){cout<<TAG<<stage<<"File blk: Correct access time "<<endl;atime = cur;isDirty = true;}
        else if (mtime>cur){cout<<TAG<<stage<<"File blk: Correct modify time "<<endl;mtime = cur;isDirty = true;}
        for(vector<string>::const_iterator i = blocks.begin(); i != blocks.end(); ++i) 
        {
            unsigned int bl = atol((*i).c_str());
            MARK_OCCUPIED(bl,true);
        }
        if (indirect != isIndirect) 
        {
            cout<<TAG<<stage<<"file indirectness is wrong , correct it!"<<endl;
            indirect =isIndirect;
            isDirty = true;
        }
        if (indirect == 0)
        {
            if (!(size >0 && size <= BLOCK_SZ))
            {
                cout<<TAG<<stage<<"size is invalid"<<endl;
                size = BLOCK_SZ;
                isDirty = true;
            }
        }
        else
        {
            if (!(size>=0 && size<=(BLOCK_SZ *(numBlks))))
            {
                cout<<TAG<<stage<<"size is invalid!!"<<numBlks<<endl;
                size = BLOCK_SZ*numBlks;
                isDirty = true;
            }
        }
        save();
        return ret;
    }
    
    virtual int parse(string loc)
    {
        int ret = SUCCESS;   
        fileLoc = loc;
        string entry;
        ret = readBlock(inoNum,fileLoc,entry);
        ret = csvParse(entry,this);
        return ret;
    }
    
    int chkIndirBlks(string entry,vector<string> &blocks)
    {
        stringstream lineStream(entry);
        string temp;
        int locLen=0;
        int isIndir = 0;
        int isDigitNess=0;
        while(std::getline(lineStream,temp,','))
        {
            blocks.push_back(temp);
            if (std::find_if(temp.begin(), temp.end(), (int(*)(int))std::isdigit) != temp.end())
            {
              isDigitNess++;
            }
            locLen++;
        }
        if (isDigitNess == locLen)
            isIndir = 1;
        return isIndir;
    }
    
};

int walk(AbstractNode * node)
{
    int ret = SUCCESS; 
    typedef map<string,int>::iterator it_type;
    DirBlock * dirBlk = (DirBlock *)node;
    string fileLoc = dirBlk->fileLoc;
    map<string,int> &dirs = dirBlk->dirs;
    
    for(it_type iterator = dirs.begin(); iterator != dirs.end(); iterator++) 
    {
        cout<<TAG<<stage<<"Checking dir Inode - "<<iterator->second<<endl;
        DirBlock dir(iterator->second);
        ret = dir.parse(dirBlk->fileLoc);
        if (ret != SUCCESS)
        {
            cout<<TAG<<stage<<"invalid syntax so this dir block "<<iterator->second<<endl;
            continue;
        }
        int ino_num = dir.inoNum;
        MARK_OCCUPIED(ino_num,true);
        if(!strcmp(iterator->first.c_str(),".")==0 && !strcmp(iterator->first.c_str(),"..")==0)
            dir.checkPrevAndCur(dirBlk);
        ret= dir.chkAndRepair();
        ret = walk(&dir);
    }
    map<string,int> &files = dirBlk->files;
    
    for(it_type iterator = files.begin(); iterator != files.end(); iterator++) 
    {
        cout<<TAG<<stage<<"Checking File Inode - "<<iterator->second<<endl;
        FileBlock fileBlk (iterator->second);
        ret = fileBlk.parse(fileLoc);
        if (ret != SUCCESS)
        {
            cout<<TAG<<stage<<"invalid syntax so this file block "<<iterator->second<<endl;
            continue;
        }
        MARK_OCCUPIED(fileBlk.inoNum,true);
        ret= fileBlk.chkAndRepair();
    }
    return ret;
}

string retAbsPath(string loc,string pre,string bl_no)
{
    return loc+pre+bl_no;
}

/** Stage 1
 */
int stage1(string loc,AbstractNode *sup)
{
    int ret = SUCCESS;
    stage = " 1 ";
    if ((ret = sup->parse(loc)) == SUCCESS)
    {
        cout<<TAG<<stage<<"Super block read succeed"<<endl;
        if((ret = sup->chkAndRepair())!=SUCCESS)
            cout<<TAG<<stage<<"stage 1 fail"<<endl;
    }
    else
        cout<<TAG<<stage<<"super block corrupt"<<endl;
    return ret;
}

/** Stage 2
 */
int stage2(string loc,AbstractNode *root)
{
    int ret = SUCCESS;
    stage = " 2 ";
    if ((ret = root->parse(loc)) == SUCCESS)
    {
        cout<<TAG<<stage<<"Root dir block read succeed"<<endl;
        ret= root->chkAndRepair();
        if(ret == SUCCESS)
        {
            int ino_num = ((DirBlock *)root)->inoNum;
            MARK_OCCUPIED(ino_num,true);
            ret = walk(root);
        }
    }
    else
        cout<<TAG<<stage<<"Root dir corrupt"<<endl;
    return ret;
}

/*Stage 3
 */
int stage3(string fileloc ,SuperBlock *superBlock)
{
    //make sure of two things , free block list must contain free items and used blocks must not be in free block list
    unsigned int bl_no;
    unsigned int freeStart = superBlock->freeStart;
    unsigned int freeEnd = superBlock->freeEnd;
    int ret = SUCCESS;
    stage = " 3 ";
    for(bl_no = 0; bl_no<MAX_BLOCKS ;bl_no++)
    {
        bool flag = blockBitmap[bl_no];
        string entry;
        string temp;
        vector<unsigned int> blocks;
        int blk = (bl_no/MAX_POINTERS)+freeStart;
        if(flag == true)//inuse
        {
            
            ret = readBlock(blk,fileloc,entry);
            string temp;
            vector<string> blocks;
            stringstream lineStream(entry);
            int loc = 0;
            int kfinalLoc=-1;
            int found =0;
            while(std::getline(lineStream,temp,','))
            {
                unsigned int block = atol(temp.c_str());
                if(bl_no == block) {found =1;kfinalLoc=loc;};
                blocks.push_back(string(temp));
                loc++;
            }
            if (found == 0)continue;
            else
            {
                cout<<TAG<<stage<<" Removing a used block "<<bl_no<<" from free block list"<<endl;
                temp.clear();
                blocks.erase (blocks.begin()+kfinalLoc);
                for (unsigned i=0; i<blocks.size(); ++i)
                {
                    temp+=blocks[i];
                    if(i != blocks.size()-1)
                        temp+=string(", ");
                }    
                ret = fileWrite(fileloc,blk,temp);
            }
        }
        else//not used
        {
            if(!(blk >=freeStart) && !(blk<=freeEnd))continue;
            ret = readBlock(blk,fileloc,entry);
            stringstream lineStream(entry);
            int found =0;
            unsigned int block;
            while(std::getline(lineStream,temp,','))
            {
                block = atol(temp.c_str());
                if(bl_no == block) found =1;
                blocks.push_back(atol(temp.c_str()));
            }
            if (found == 1)continue;
            else
            {
                cout<<TAG<<stage<<"adding an unused block "<<bl_no<<" to free block list"<<endl;
                temp.clear();
                vector<unsigned int>::iterator it;
                it = blocks.begin();
                blocks.push_back(bl_no);
                std::sort (blocks.begin(), blocks.end());
                for (unsigned i=0; i<blocks.size(); ++i)
                {
                    std::stringstream tempss;
                    tempss<<blocks[i];
                    temp+=string(tempss.str());
                    if(i != blocks.size()-1)
                        temp+=string(", ");
                }    
                ret = fileWrite(fileloc,blk,temp);
            }
        }
    }
    return ret;
}

int csefsck(string loc)
{
    int ret = SUCCESS;
    SuperBlock superBlock(SUPER_BLOCK_LOC);
    int ino_num = superBlock.inoNum;
    MARK_OCCUPIED(ino_num,true);
    if(stage1(loc,&superBlock)==SUCCESS)//stage 1 Will verify requirement 1 and 2
    {
        cout<<TAG<<stage<<"stage 1 verified - Super block is ok"<<endl;
        int root = superBlock.getRoot();
        DirBlock rootDirBlk (root);
        rootDirBlk.isRoot=1;
        if(stage2(loc,&rootDirBlk)==SUCCESS)//stage 2 Will verify requirement 5 6 and 7
        {
            cout<<TAG<<stage<<"stage 2 verified - Directory and File inodes ok"<<endl;
            if(stage3(loc,&superBlock)==SUCCESS)//stage 3 Will verify requirement 3 and 4
            {
               cout<<TAG<<stage<<"stage 3 verified - Free block lists ok"<<endl;
            }
        }
    }
    else
        cout<<TAG<<stage<<"super block error!"<<endl;
    return ret;
}

int main(int argc, char** argv) 
{
    string filenamePre;
    int ret = SUCCESS;
    
    cout<<"csefcsk ver 1.0"<<endl;
    if(argc == 2)
    {
        cout<<argv[1]<<endl;
        filenamePre = string(argv[1]);
    }
    else
    {
        cout<<"Error in usage: csefcsk <location[eg- F:\\fusedata\\]> , fallback to default loc"<<endl;
        filenamePre = test_loc;
    }
    ret = csefsck(filenamePre);
    cout<<"csefsck exit"<<endl;
    return ret;
}