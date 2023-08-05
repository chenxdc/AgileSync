/*
 *  程序名：webserver3.cpp，数据服务总线服务。
 *  http://49.232.185.49:5008?username=ty&passwd=typwd&intername=getzhobtmind3&obtid=50632&begintime=20210613200000&endtime=20210613210000
 *  http://49.232.185.49:5008?username=ty&passwd=typwd&intername=getzhobtmind3&obtid=59981&begintime=20210613200000&endtime=20210613210000
    tmp.sh中有脚本。
    采用了数据库连接池。

 *  作者：吴从周。
*/
#include "_public.h"
#include "_mysql.h"

void *pthmain(void *arg);  // 线程主函数。

vector<long> vpthid;       // 存放线程id的容器。

void mainexit(int sig);    // 信号2和15的处理函数。

void pthexit(void *arg);   // 线程清理函数。
 
CLogFile logfile;          // 服务程序的运行日志。
CTcpServer TcpServer;      // 创建服务端对象。

// 读取客户端的报文。
int ReadT(const int sockfd,char *buffer,const int size,const int itimeout);

struct st_arg
{
  char connstr[101];  // 数据库的连接参数。
  char charset[51];   // 数据库的字符集。
  int  port;          // web服务监听的端口。
  int  timeout;       // 线程超时时间。
} starg;

// 显示程序的帮助
void _help(char *argv[]);

// 把xml解析到参数starg结构中
bool _xmltoarg(char *strxmlbuffer);

// 从GET请求中获取参数。
bool getvalue(const char *buffer,const char *name,char *value,const int len);
// 判断用户名和密码。
bool Login(connection *conn,const char *buffer,const int sockfd);
// 判断用户是否有调用接口的权限。
bool CheckPerm(connection *conn,const char *buffer,const int sockfd);
// 执行接口的sql语句，把数据返回给客户端。
bool ExecSQL(connection *conn,const char *buffer,const int sockfd);

#define MAXCONNS 10                  // 数据库连接池的大小。
pthread_mutex_t mutex[MAXCONNS];     // 锁数组。
connection conns[MAXCONNS];          // 数据库连接数组。
bool initconns();                    // 初始数据库连接池。
connection *getconns();              // 从连接池中获取一个数据库连接。
bool freeconns(connection *in_conn); // 释放数据库连接。

int main(int argc,char *argv[])
{
  if (argc!=3) { _help(argv); return -1; }

  // 关闭全部的信号和输入输出
  //CloseIOAndSignal();

  // 处理程序退出的信号
  signal(SIGINT,mainexit); signal(SIGTERM,mainexit);

  if (logfile.Open(argv[1],"a+")==false)
  {
    printf("打开日志文件失败（%s）。\n",argv[1]); return -1;
  }
 
  // 把xml解析到参数starg结构中
  if (_xmltoarg(argv[2])==false)  mainexit(-1);

  // 设置信号,在shell状态下可用 "kill + 进程号" 正常终止些进程
  // 但请不要用 "kill -9 +进程号" 强行终止
  signal(SIGINT,mainexit); signal(SIGTERM,mainexit);
 
  if (TcpServer.InitServer(starg.port)==false) // 初始化TcpServer的通信端口。
  {
    logfile.Write("TcpServer.InitServer(%s) failed.\n",starg.port); return -1;
  }

  if (initconns()==false)  // 初始化数据库连接池。
  {
    logfile.Write("initconns() failed.\n"); return -1;
  }
 
  /*
  connection conn;

  if (conn.connecttodb(starg.connstr,starg.charset) != 0)
  {
    logfile.Write("connect database(%s) failed.\n%s\n",starg.connstr,conn.m_cda.message); pthread_exit(0);
  }

  char buffer[]="GET /?username=wucz&passwd=wuczpwd&intername=getzhobtmind3&obtid=50632&begintime=20210613170000&endtime=20210613180000 HTTP/1.1";

  ExecSQL(&conn,buffer,1);

  return 0;
  */

  while (true)
  {
    if (TcpServer.Accept()==false)   // 等待客户端连接。
    {
      logfile.Write("TcpServer.Accept() failed.\n"); continue;
    }

    logfile.Write("客户端(%s)已连接。\n",TcpServer.GetIP());

    pthread_t pthid;
    if (pthread_create(&pthid,NULL,pthmain,(void *)(long)TcpServer.m_connfd)!=0)
    { logfile.Write("pthread_create failed.\n"); mainexit(-1); }

    vpthid.push_back(pthid); // 把线程id保存到vpthid容器中。
  }

  return 0;
}

void *pthmain(void *arg)
{
  pthread_cleanup_push(pthexit,arg);  // 设置线程清理函数。

  pthread_detach(pthread_self());  // 分离线程。

  pthread_setcanceltype(PTHREAD_CANCEL_DISABLE,NULL);  // 设置取消方式为立即取消。

  int sockfd=(int)(long)arg;  // 与客户端的socket连接。

  int  ibuflen=0;
  char strrecvbuf[1024],strsendbuf[1024];

  memset(strrecvbuf,0,sizeof(strrecvbuf));

  // 读取客户端的报文，如果超时或失败，线程退出。
  if (ReadT(sockfd,strrecvbuf,sizeof(strrecvbuf),starg.timeout)<=0) pthread_exit(0);

  // 如果不是GET请求报文不处理，线程退出。
  if (strncmp(strrecvbuf,"GET",3)!=0)  pthread_exit(0);

  logfile.Write("%s\n\n",strrecvbuf);

  connection *conn=getconns();  // 获取一个数据库连接。

  // 判断用户名和密码。
  if (Login(conn,strrecvbuf,sockfd)==false) { freeconns(conn); pthread_exit(0); }

  // 判断用户是否有调用接口的权限。
  if (CheckPerm(conn,strrecvbuf,sockfd)==false) { freeconns(conn); pthread_exit(0); }

  // 先把响应报文头部发送给客户端。
  memset(strsendbuf,0,sizeof(strsendbuf));
  sprintf(strsendbuf,\
         "HTTP/1.1 200 OK\r\n"\
         "Server: webserver\r\n"\
         "Content-Type: text/html;charset=gbk\r\n\r\n"\
         "<retcode>0</retcode><message>ok</message>\n");
  Writen(sockfd,strsendbuf,strlen(strsendbuf));

  // 再执行接口的sql语句，把数据返回给客户端。
  if (ExecSQL(conn,strrecvbuf,sockfd)==false) { freeconns(conn); pthread_exit(0); }

  freeconns(conn); 
  
  pthread_cleanup_pop(1);

  pthread_exit(0);
}

// 信号2和15的处理函数。
void mainexit(int sig)
{
  // logfile.Write("mainexit begin.\n");

  // 关闭监听的socket。
  TcpServer.CloseListen();

  // 取消全部的线程。
  for (int ii=0;ii<vpthid.size();ii++)
  {
    logfile.Write("cancel %ld\n",vpthid[ii]);
    pthread_cancel(vpthid[ii]);
  }

  // logfile.Write("mainexit end.\n");

  // 释放数据库连接池。
  for (int ii=0;ii<MAXCONNS;ii++)
  {
    logfile.Write("disconnect and pthread_mutex_destroy.\n");
    conns[ii].disconnect();
    pthread_mutex_destroy(&mutex[ii]);
  }

  exit(0);
}

// 线程清理函数。
void pthexit(void *arg)
{
  // logfile.Write("pthexit begin.\n");

  // 关闭与客户端的socket。
  close((int)(long)arg);

  // 从vpthid中删除本线程的id。
  for (int ii=0;ii<vpthid.size();ii++)
  {
    if (vpthid[ii]==pthread_self())
    {
      vpthid.erase(vpthid.begin()+ii);
    }
  }

 // logfile.Write("pthexit end.\n");
}

// 显示程序的帮助
void _help(char *argv[])
{
  printf("Using:/project/tools/bin/webserver3 logfilename xmlbuffer\n\n");

  printf("Sample:/project/tools/bin/procctl 10 /project/tools/bin/webserver3 /log/idc/webserver3.log \"<connstr>127.0.0.1,root,mysqlpwd,mysql,3306</connstr><charset>gbk</charset><port>5008</port><timeout>5</timeout>\"\n\n");

  printf("本程序是数据总线的服务端程序，为数据中心提供http协议的数据访问接口。\n");
  printf("logfilename 本程序运行的日志文件。\n");
  printf("xmlbuffer   本程序运行的参数，用xml表示，具体如下：\n\n");

  printf("connstr     数据库的连接参数，格式：ip,username,password,dbname,port。\n");
  printf("charset     数据库的字符集，这个参数要与数据源数据库保持一致，否则会出现中文乱码的情况。\n");
  printf("port        web服务监听的端口。\n");
  printf("timeout     线程超时的时间，单位：秒，建议取值小于10。\n\n");
}

// 把xml解析到参数starg结构中
bool _xmltoarg(char *strxmlbuffer)
{
  memset(&starg,0,sizeof(struct st_arg));

  GetXMLBuffer(strxmlbuffer,"connstr",starg.connstr,100);
  if (strlen(starg.connstr)==0) { logfile.Write("connstr is null.\n"); return false; }

  GetXMLBuffer(strxmlbuffer,"charset",starg.charset,50);
  if (strlen(starg.charset)==0) { logfile.Write("charset is null.\n"); return false; }

  GetXMLBuffer(strxmlbuffer,"port",&starg.port);
  if (starg.port==0) { logfile.Write("port is null.\n"); return false; }

  GetXMLBuffer(strxmlbuffer,"timeout",&starg.timeout);
  if (starg.timeout==0) { logfile.Write("timeout is null.\n"); return false; }

  return true;
}

// 读取客户端的报文。
int ReadT(const int sockfd,char *buffer,const int size,const int itimeout)
{
  if (itimeout > 0)
  {
    fd_set tmpfd;

    FD_ZERO(&tmpfd);
    FD_SET(sockfd,&tmpfd);

    struct timeval timeout;
    timeout.tv_sec = itimeout; timeout.tv_usec = 0;

    int iret;
    if ( (iret = select(sockfd+1,&tmpfd,0,0,&timeout)) <= 0 ) return iret;
  }

  return recv(sockfd,buffer,size,0);
}

// 从GET请求中获取参数。
bool getvalue(const char *buffer,const char *name,char *value,const int len)
{
  value[0]=0;

  char *start,*end;
  start=end=0;

  start=strstr((char *)buffer,(char *)name);
  if (start==0) return false;

  end=strstr(start,"&");
  if (end==0) end=strstr(start," ");

  if (end==0) return false;

  int ilen=end-(start+strlen(name)+1);
  if (ilen>len) ilen=len;

  strncpy(value,start+strlen(name)+1,ilen);

  value[ilen]=0;

  return true;
}

// 判断用户名和密码。
bool Login(connection *conn,const char *buffer,const int sockfd)
{
  char username[31],passwd[31];
  
  getvalue(buffer,"username",username,30); // 获取用户名。
  getvalue(buffer,"passwd",passwd,30);     // 获取密码。

  sqlstatement stmt;
  stmt.connect(conn);
  stmt.prepare("select count(*) from T_USERINFO where username=:1 and passwd=:2 and rsts=1");
  stmt.bindin(1,username,30);
  stmt.bindin(2,passwd,30);
  int icount=0;
  stmt.bindout(1,&icount);
  stmt.execute();
  stmt.next();

  // logfile.Write("username=%s,passwd=%s,icount=%d\n",username,passwd,icount);

  if (icount!=1)
  {
    char strbuffer[256];
    memset(strbuffer,0,sizeof(strbuffer));
   
    sprintf(strbuffer,\
           "HTTP/1.1 200 OK\r\n"\
           "Server: webserver\r\n"\
           "Content-Type: text/html;charset=gbk\r\n\n\n"\
           "<retcode>-1</retcode><message>username or passwd is invailed</message>");
    Writen(sockfd,strbuffer,strlen(strbuffer));
    
    return false;
  }
  
  return true;
}

// 判断用户是否有调用接口的权限。
bool CheckPerm(connection *conn,const char *buffer,const int sockfd)
{
  char username[31],intername[30];
  
  getvalue(buffer,"username",username,30);    // 获取用户名。
  getvalue(buffer,"intername",intername,30);  // 获取接口名。

  sqlstatement stmt;
  stmt.connect(conn);
  stmt.prepare("select count(*) from T_USERANDINTER where username=:1 and intername=:2 and intername in (select intername from T_INTERCFG where rsts=1)");
  stmt.bindin(1,username,30);
  stmt.bindin(2,intername,30);
  int icount=0;
  stmt.bindout(1,&icount);
  stmt.execute();
  stmt.next();

  // logfile.Write("username=%s,intername=%s,icount=%d\n",username,intername,icount);

  if (icount!=1)
  {
    char strbuffer[256];
    memset(strbuffer,0,sizeof(strbuffer));
  
    sprintf(strbuffer,\
           "HTTP/1.1 200 OK\r\n"\
           "Server: webserver\r\n"\
           "Content-Type: text/html;charset=gbk\r\n\n\n"\
           "<retcode>-1</retcode><message>permission denied</message>");

    Writen(sockfd,strbuffer,strlen(strbuffer));
    
    return false;
  }
  
  return true;
}

// 执行接口的sql语句，把数据返回给客户端。
bool ExecSQL(connection *conn,const char *buffer,const int sockfd)
{
  char username[31],intername[30],selectsql[1001],colstr[301],bindin[301];
  memset(username,0,sizeof(username));
  memset(intername,0,sizeof(intername));
  memset(selectsql,0,sizeof(selectsql)); // 接口SQL。
  memset(colstr,0,sizeof(colstr));       // 输出列名。
  memset(bindin,0,sizeof(bindin));       // 接口参数。

  getvalue(buffer,"username",username,30);    // 获取用户名。
  getvalue(buffer,"intername",intername,30);  // 获取接口名。
  
  // 把接口的参数取出来。
  sqlstatement stmt;
  stmt.connect(conn);
  stmt.prepare("select selectsql,colstr,bindin from T_INTERCFG where intername=:1");
  stmt.bindin(1,intername,30);     // 接口名。
  stmt.bindout(1,selectsql,1000);  // 接口SQL。
  stmt.bindout(2,colstr,300);      // 输出列名。
  stmt.bindout(3,bindin,300);      // 接口参数。
  stmt.execute();  // 这里基本上不用判断返回值，出错的可能几乎没有。
  stmt.next();

  // prepare接口的SQL语句。
  stmt.prepare(selectsql);
  
  CCmdStr CmdStr;

  ////////////////////////////////////////
  // 拆分输入参数bindin。
  CmdStr.SplitToCmd(bindin,",");

  // 用于存放输入参数的数组，输入参数的值不会太长，100足够。
  char invalue[CmdStr.CmdCount()][101];
  memset(invalue,0,sizeof(invalue));

  // 从http的GET请求报文中解析出输入参数，绑定到sql中。
  for (int ii=0;ii<CmdStr.CmdCount();ii++)
  {
    getvalue(buffer,CmdStr.m_vCmdStr[ii].c_str(),invalue[ii],100); 
    stmt.bindin(ii+1,invalue[ii],100);
  }
  ////////////////////////////////////////

  ////////////////////////////////////////
  // 拆分colstr，可以得到结果集的字段数。
  CmdStr.SplitToCmd(colstr,",");

  // 用于存放结果集的数组。
  char colvalue[CmdStr.CmdCount()][2001];
  
  // 把结果集绑定到colvalue数组。
  for (int ii=0;ii<CmdStr.CmdCount();ii++)
  {
    stmt.bindout(ii+1,colvalue[ii],2000);
  }
  ////////////////////////////////////////

  if (stmt.execute() != 0)
  {
    logfile.Write("stmt.execute() failed.\n%s\n%s\n",stmt.m_sql,stmt.m_cda.message); return false;
  }

  // logfile.WriteEx("<data>\n");
  Writen(sockfd,"<data>\n",strlen("<data>\n"));

  char strsendbuffer[4001],strtemp[2001];

  // 逐行获取结果集，发送给客户端。
  while (true)
  {
    memset(strsendbuffer,0,sizeof(strsendbuffer));
    memset(colvalue,0,sizeof(colvalue));

    if (stmt.next() != 0) break;

    for (int ii=0;ii<CmdStr.CmdCount();ii++)
    {
      memset(strtemp,0,sizeof(strtemp));
      snprintf(strtemp,2000,"<%s>%s</%s>",CmdStr.m_vCmdStr[ii].c_str(),colvalue[ii],CmdStr.m_vCmdStr[ii].c_str());
      strcat(strsendbuffer,strtemp);
      // logfile.WriteEx("<%s>%s</%s>",CmdStr.m_vCmdStr[ii].c_str(),colvalue[ii],CmdStr.m_vCmdStr[ii].c_str());
      
    }

    // logfile.WriteEx("<endl/>\n");
    strcat(strsendbuffer,"<endl/>\n");
    Writen(sockfd,strsendbuffer,strlen(strsendbuffer));
  }

  //logfile.WriteEx("</data>\n");
  Writen(sockfd,"</data>\n",strlen("</data>\n"));

  logfile.Write("intername=%s,count=%d\n",intername,stmt.m_cda.rpc);
  
  return true;
}

bool initconns()   // 初始数据库连接池
{
  for (int ii=0;ii<MAXCONNS;ii++)
  {
    logfile.Write("%d,connecttodb and pthread_mutex_init.\n",ii);

    // 连接数据库
    if (conns[ii].connecttodb(starg.connstr,starg.charset) != 0)
    {
      logfile.Write("connect database(%s) failed.\n%s\n",starg.connstr,conns[ii].m_cda.message); 
      return false;
    }

    pthread_mutex_init(&mutex[ii],0); // 创建锁
  }

  return true;
}

connection *getconns()
{
  // for (int jj=0;jj<1000;jj++)
  while (true)
  {
    for (int ii=0;ii<MAXCONNS;ii++)
    {
      if (pthread_mutex_trylock(&mutex[ii])==0)
      {
        // logfile.Write("jj=%d,ii=%d\n",jj,ii);
        logfile.Write("get conns is %d.\n",ii);
        return &conns[ii];
      }
    }

    usleep(10000);
  }
}

bool freeconns(connection *in_conn)
{
  for (int ii=0;ii<MAXCONNS;ii++)
  {
    if (in_conn==&conns[ii])
    {
      logfile.Write("free conn %d.\n",ii);
      pthread_mutex_unlock(&mutex[ii]); in_conn=0; return true;
    }
  }

  return false;
}

