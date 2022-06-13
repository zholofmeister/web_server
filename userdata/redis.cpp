#include "redis.h"
#include <map>

redis_clt *redis_clt::m_redis_instance = new redis_clt();

redis_clt *redis_clt::getinstance()
{
    return m_redis_instance;
}

string redis_clt::getReply(string m_command)
{
    m_redis_lock.dolock();
    m_redisReply = (redisReply *)redisCommand(m_redisContext, m_command.c_str());
    string temp = "";
    if (m_redisReply->elements == 0 && m_redisReply->type == 1) { //string
        if (m_redisReply->len > 0) { 
            temp = string(m_redisReply->str);
            //cerr << "reply:   " << temp << endl;
        }
        else
            cerr << "return nothing?" << endl;
    } else if (m_redisReply->type == 3){  //int
        int tempcode = m_redisReply->integer;
        temp = to_string(tempcode);
    } else { //数组
        //for post
        temp += "{";
        for (int i = 0; i < m_redisReply->elements; ++i){
            temp += "\""; //转义"
            temp += string(m_redisReply->element[i]->str);
            temp += "\"";
            if (i % 2 == 0)
                temp += ":";
            else
                temp += ",";
        }
        temp.pop_back();
        temp += "}";
    }
    freeReplyObject(m_redisReply);
    m_redis_lock.unlock();
    return temp;
}

redis_clt::redis_clt()
{
    timeout = {2, 0};
    m_redisContext = (redisContext *)redisConnectWithTimeout("127.0.0.1", 6379, timeout);
    m_redisReply = nullptr;
    board_exist(); //如果没有COLOR的话，创建并初始化COLOR
}
string redis_clt::getvoteboard()
{
    string board = getReply("zrange COLOR 0 -1 withscores");
    //验证返回格式是否正确
    if (board[0] == '{' && board[board.length() - 1] == '}'){
        return board;
    } else {
        return "";
    }
}
void redis_clt::board_exist()
{
    string board;
    board = getReply("EXISTS COLOR");
    //如果不存在COLOR的话，创建并初始化COLOR
    if (board != "1") {
        getReply("DEL COLOR");
        getReply("zadd COLOR 0 yellow");
        getReply("zadd COLOR 0 orange");
        getReply("zadd COLOR 0 red");
        getReply("zadd COLOR 0 pink");
        getReply("zadd COLOR 0 blue");
        getReply("zadd COLOR 0 green");
        cerr << "init ok" << endl;
    }
}
/*
int main()
{
    redis_clt *m_r = redis_clt::getinstance();
    m_r->board_exist();
    //cout << temp << endl;

    return 0;
}
*/
