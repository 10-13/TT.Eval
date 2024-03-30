#pragma once
#ifndef EVALCORE_H_HPP
#define EVALCORE_H_HPP
#include <vector>
#include <string>
#include <sstream>
#include <stack>
#include <memory>
#include <set>
#include <functional>

template <typename T>
using sp = std::shared_ptr<T>;

#define is_branch(x) x->Depth() > 0
#define is_value(x) x->Depth() == 0
#define as_branch(x) std::dynamic_pointer_cast<Branch>(x)
#define as_value(x) std::dynamic_pointer_cast<Value>(x)

#pragma region Exceptions
class ExecutionEngineException : public std::exception
{
public:
    enum class Level
    {
        Warning,
        Minor,
        Critical,
        Fatal,
    };

    ExecutionEngineException(
        std::exception* base,
        std::string what = "Execution engine exception",
        Level level = Level::Fatal)
        : base_(base),
        what_(what),
        err_level_(level)
    {
        switch (level)
        {
        case ExecutionEngineException::Level::Warning:
            what += "[Warning]";
            break;
        case ExecutionEngineException::Level::Minor:
            what += "[Minor]";
            break;
        case ExecutionEngineException::Level::Critical:
            what += "[Critical]";
            break;
        case ExecutionEngineException::Level::Fatal:
            what += "[Fatal]";
            break;
        }
        if (base != nullptr)
            what_ += base->what();
    }

    const char* what() const noexcept override
    {
        return what_.c_str();
    }

    Level ErrorLevel()
    {
        return err_level_;
    }

    std::exception* BaseException()
    {
        return base_;
    }

    static void ThrowWraped(std::string msg, Level level = Level::Fatal)
    {
        throw new ExecutionEngineException(new ExecutionEngineException(nullptr, msg, level));
    }

private:
    std::exception* base_;
    std::string what_{};
    Level err_level_{ Level::Fatal };
};
#pragma endregion

class BranchBase
{
public:
    virtual unsigned int Depth() const noexcept = 0;
    virtual sp<BranchBase> Copy() const noexcept = 0;
};

class Branch : public BranchBase
{
public:
    std::vector<sp<BranchBase>> Branches{};

    Branch() noexcept = default;
    Branch(std::stack<sp<BranchBase>>& stack, int taken) noexcept
    {
        Branches.resize(taken);
        for (int i = taken - 1; i > -1; i--)
        {
            Branches[i] = stack.top();
            stack.pop();
        }
    }

    void Unpack(std::stack<sp<BranchBase>>& stack)
    {
        for (auto i : Branches)
            stack.push(i->Copy());
        Branches.clear();
    }

    unsigned int Depth() const noexcept override
    {
        int max = 0;
        for (auto i : Branches)
            if (i->Depth() > max)
                max = i->Depth();
        return max + 1;
    }

    virtual sp<BranchBase> Copy() const noexcept override
    {
        sp<Branch> res = std::make_shared<Branch>();
        res->Branches.resize(Branches.size());
        for (int i = 0; i < Branches.size(); i++)
            res->Branches[i] = Branches[i]->Copy();
        return res;
    }
};

class Value : public BranchBase
{
public:
    std::string Stored{ "" };

    Value() noexcept = default;
    Value(const std::string& row) noexcept : Stored(row) {}

    bool IsEmpty()
    {
        return Stored.size() == 0 || Stored == "";
    }

    unsigned int Depth() const noexcept override
    {
        return 0;
    }

    sp<BranchBase> Copy() const noexcept override
    {
        return std::make_shared<Value>(Stored);
    }

    template<typename TargetType>
    TargetType ReadAs()
    {
        TargetType res;
        std::stringstream s(Stored);
        s >> res;
        return res;
    }
};

class BranchStream
{
private:
    std::ostream& out_;
    int depth_{ 0 };

public:
    std::string Space = "\t";
    std::string Section = "./section";
    std::string ValueEnd = "\n";

    BranchStream(std::ostream& out) : out_(out) {}

    BranchStream& operator<<(sp<Value> value)
    {
        for (int i = 0; i < depth_; i++)
            out_ << Space;
        out_ << value->Stored << ValueEnd;
        return *this;
    }

    BranchStream& operator<<(sp<Branch> branch)
    {
        for (int i = 0; i < depth_; i++)
            out_ << Space;
        depth_++;
        out_ << Section << ValueEnd;
        for (auto i : branch->Branches)
            *this << i;
        depth_--;
        return *this;
    }

    BranchStream& operator<<(sp<BranchBase> branch)
    {
        if (branch->Depth() == 0)
            *this << std::dynamic_pointer_cast<Value>(branch);
        else
            *this << std::dynamic_pointer_cast<Branch>(branch);
        return *this;
    }
};

class Evaluator
{
public:
    using Function = std::function<void(Evaluator* eval)>;
    struct FuncDef
    {
        Function Func;
        std::string Name;

        FuncDef(Function f, std::string n) : Func(f), Name(n) {}

        bool operator<(const FuncDef& other) const noexcept
        {
            return Name < other.Name;
        }
        bool operator==(const FuncDef& other) const noexcept
        {
            return Name == other.Name;
        }
    };

    std::set<FuncDef> Functions;
    std::stack<sp<BranchBase>> Data;
    std::stack<std::string> Log;

    ExecutionEngineException::Level ApprovedLevel;

    Evaluator() {}

    void LoadDefault()
    {
        // ^ - pack operation
        // t - top only operation
        // c - count argument
        // d - depth argument
        // i - index argument
        // g - grouped operation
        // _ - reverse operation
        // | - generative operation
        // M - math operations
        // Y - tree operations
        // S - statistics operations
        // ? - logical operations
        // # - remove operation
        // $ - row operation
        Functions.insert({ PackTop, "^t" });
        Functions.insert({ PackTopSameLevel, "^" });
        Functions.insert({ UnpackTop, "^_t" });
        Functions.insert({ PackTopX, "^tc" });

        Functions.insert({ EmptyBranch, "|Eb" });
        Functions.insert({ EmptyElement , "|Ev" });
        Functions.insert({ CopyFromIndex, "|i" });
        Functions.insert({ ExtractColumnPack, "|id" }) ;
        Functions.insert({ CopyFromIndex, "|[" });
        Functions.insert({ ExtractColumnPack, "|]" }) ;
        Functions.insert({ ExtractGroupedColumnPack, "|]g" }) ;

        Functions.insert({ Copy, "|" });
        Functions.insert({ Duplicate, "|c" });

        Functions.insert({ [](Evaluator* eval) { eval->Data.pop(); }, "#" });
        Functions.insert({ DeepRemove, "#d" });

        Functions.insert({ Undot, "$" });
        Functions.insert({ ConcatRow, "$^" });
        Functions.insert({ SplitRow, "$_" });

        Functions.insert({ Reverse, "_" });
    }

    void EvalCom(std::string com, bool log = true, bool catch_localy = true)
    {
        try
        {
            if (com.size() < 1)
                return;
            auto it = Functions.find({ nullptr,com });
            if (it == Functions.end())
                Data.push(std::make_shared<Value>(com));
            else
                it->Func(this);
        }
        catch (ExecutionEngineException* exception)
        {
            if (!catch_localy)
                throw;
            /*(Create log)*/
            {
                std::stringstream str;
                str << exception->what() << "\nCaused during invoking:" << com;
                if (log)
                    Log.push(str.str());
            }
            if (exception->ErrorLevel() > ApprovedLevel)
                throw;
        }
    }

    void EvalComs(std::vector<std::string> coms)
    {
        for (int i = 0; i < coms.size(); i++)
        {
            try
            {
                EvalCom(coms[i], false, false);
            }
            catch (ExecutionEngineException* exception)
            {
                /*Create log*/
                {
                    std::stringstream str;
                    str << exception->what() << "\nCaused during invoking:" << coms[i];
                    str << "\nCom trace:";
                    for (int j = 0; j < i + 1; j++)
                        str << "\t" << coms[i] << "\n";
                    Log.push(str.str());
                }
                if (exception->ErrorLevel() > ApprovedLevel)
                    throw;
            }
        }
    }

    void PrintData(std::ostream& out)
    {
        BranchStream str{ out };
        std::stack<sp<BranchBase>> tmp;
        while (!Data.empty())
        {
            tmp.push(Data.top());
            str << tmp.top();
            Data.pop();
        }
        while (!tmp.empty())
        {
            Data.push(tmp.top());
            tmp.pop();
        }
    }

    static void RequireValue(sp<BranchBase> br)
    {
        if (!is_value(br))
            ExecutionEngineException::ThrowWraped("Branch as value argument", ExecutionEngineException::Level::Critical);
    }

    static void RequireBranch(sp<BranchBase> br)
    {
        if (!is_branch(br))
            ExecutionEngineException::ThrowWraped("Value as branch argument", ExecutionEngineException::Level::Critical);
    }

    static void RequireInteger(sp<BranchBase> br)
    {
        RequireValue(br);
        auto v = as_value(br);
        if (v->Stored.size() > 8)
            ExecutionEngineException::ThrowWraped("Number larger than integer", ExecutionEngineException::Level::Critical);
        if (v->IsEmpty())
            ExecutionEngineException::ThrowWraped("Passing empty as number", ExecutionEngineException::Level::Critical);
        for (int i = 0; i < v->Stored.size(); i++)
            if (v->Stored[i] < '0' || v->Stored[i] > '9')
                ExecutionEngineException::ThrowWraped("Not a number passed as an integer", ExecutionEngineException::Level::Critical);
    }

    void RequireTop(int sz = 1)
    {
        if (Data.size() < sz)
            ExecutionEngineException::ThrowWraped("Required argument, but not passed", ExecutionEngineException::Level::Critical);
    }

    void RequireValueTop()
    {
        RequireTop();
        RequireValue(Data.top());
    }

    void RequireIntegerTop()
    {
        RequireTop();
        RequireInteger(Data.top());
    }

    void RequireBranchTop()
    {
        RequireTop();
        RequireBranch(Data.top());
    }

    static void Require(std::string request, sp<BranchBase> br)
    {
        RequireBranch(br);
        std::stack<sp<Branch>> l;
        std::stack<int> c_p;
        l.push(as_branch(br));
        int index = 0;
        do
        {
            if (request[index] == '.')
            {
                l.pop();
                c_p.pop();
            }
            else if (request[index] == 'b')
            {
                RequireBranch(l.top()->Branches[c_p.top()]);
                l.push(as_branch(l.top()->Branches[c_p.top()]));
                c_p.top()++;
                c_p.push(0);
            }
            switch (request[index])
            {
            case '.':
                l.pop();
                c_p.pop();
                break;
            case 'b':
                RequireBranch(l.top()->Branches[c_p.top()]);
                l.push(as_branch(l.top()->Branches[c_p.top()]));
                c_p.top()++;
                c_p.push(0);
                break;
            case 'v':
                RequireValue(l.top()->Branches[c_p.top()]);
                c_p.top()++;
                break;
            case 'i':
                RequireInteger(l.top()->Branches[c_p.top()]);
                c_p.top()++;
                break;
            case 'e':
                l.top()->Branches[c_p.top()];
                c_p.top()++;
                break;
            default:
                ExecutionEngineException::ThrowWraped("Require syntax error", ExecutionEngineException::Level::Critical);
                break;
            }
            index++;
        } while (index < request.size());
    }

private:
#pragma region DEFAULT_STACK_OP
    static void PackTop(Evaluator* eval)
    {
        if (eval->Data.size() == 0)
            ExecutionEngineException::ThrowWraped("Required argument, but not passed", ExecutionEngineException::Level::Critical);
        eval->Data.push(std::make_shared<Branch>(eval->Data, 1));
    }
    static void UnpackTop(Evaluator* eval)
    {
        eval->RequireBranchTop();
        auto br = eval->Data.top();
        eval->Data.pop();
        as_branch(br)->Unpack(eval->Data);
    }
    static void PackTopSameLevel(Evaluator* eval)
    {
        sp<Branch> br = std::make_shared<Branch>();
        int d = eval->Data.top()->Depth();
        while (eval->Data.size() > 0 && eval->Data.top()->Depth() == d)
        {
            br->Branches.push_back(eval->Data.top());
            eval->Data.pop();
        }
        eval->Data.push(br);
    }
    static void PackTopX(Evaluator* eval)
    {
        eval->RequireValueTop();
        int c = as_value(eval->Data.top())->ReadAs<int>();
        eval->Data.pop();
        if (eval->Data.size() < c)
            ExecutionEngineException::ThrowWraped("Too few arguments to unpack", ExecutionEngineException::Level::Critical);
        auto br = std::make_shared<Branch>(eval->Data, c);
        eval->Data.push(br);
    }
    static void EmptyBranch(Evaluator* eval)
    {
        eval->Data.push(std::make_shared<Branch>());
    }
    static void EmptyElement(Evaluator* eval)
    {
        eval->Data.push(std::make_shared<Value>());
    }
    static void CopyFromIndex(Evaluator* eval)
    {
        eval->RequireValueTop();
        int c = as_value(eval->Data.top())->ReadAs<int>();
        eval->Data.pop();
        eval->RequireBranchTop();
        eval->Data.push(as_branch(eval->Data.top())->Branches[c]->Copy());
    }
    static void Undot(Evaluator* eval)
    {
        eval->RequireValueTop();
        std::string& v = as_value(eval->Data.top())->Stored;
        if (v[0] == '.')
            v = v.substr(1, v.size());
    }
    static void ExtractColumnPack(Evaluator* eval)
    {
        eval->RequireValueTop();
        int index = as_value(eval->Data.top())->ReadAs<int>();
        eval->Data.pop();
        eval->RequireValueTop();
        int depth = as_value(eval->Data.top())->ReadAs<int>();
        eval->Data.pop();
        if (depth < 1)
            ExecutionEngineException::ThrowWraped("Cannot extract from zero depth", ExecutionEngineException::Level::Critical);
        eval->RequireBranchTop();
        sp<Branch> nbranch = std::make_shared<Branch>();
        std::stack<sp<Branch>> br;
        br.push(as_branch(eval->Data.top()));
        std::stack<int> cp;
        cp.push(0);
        while (br.size() != 0)
        {
            if (cp.top() >= br.top()->Branches.size())
            {
                cp.pop();
                br.pop();
                continue;
            }
            if (br.size() == depth)
            {
                if (index < br.top()->Branches.size())
                    nbranch->Branches.push_back(br.top()->Branches[index]->Copy());
                br.pop();
                cp.pop();
                continue;
            }
            if (is_branch(br.top()->Branches[cp.top()]))
            {
                br.push(as_branch(br.top()->Branches[cp.top()]));
                cp.top()++;
                cp.push(0);
            }
            else
            {
                cp.top()++;
            }
        }
        eval->Data.push(nbranch);
    }
    static void ExtractGroupedColumnPack(Evaluator* eval)
    {
        eval->RequireValueTop();
        int index = as_value(eval->Data.top())->ReadAs<int>();
        eval->Data.pop();
        eval->RequireValueTop();
        int depth = as_value(eval->Data.top())->ReadAs<int>();
        eval->Data.pop();
        if (depth < 1)
            ExecutionEngineException::ThrowWraped("Cannot extract from zero depth", ExecutionEngineException::Level::Critical);
        eval->RequireBranchTop();
        sp<Branch> nbranch = std::make_shared<Branch>();
        std::stack<sp<Branch>> br;
        std::stack<sp<Branch>> nbr;
        nbr.push(nbranch);
        br.push(as_branch(eval->Data.top()));
        std::stack<int> cp;
        cp.push(0);
        while (br.size() != 0)
        {
            if (cp.top() >= br.top()->Branches.size())
            {
                cp.pop();
                br.pop();
                nbr.pop();
                continue;
            }
            if (br.size() == depth)
            {
                if (index < br.top()->Branches.size())
                    nbr.top()->Branches.push_back(br.top()->Branches[index]->Copy());
                br.pop();
                cp.pop();
                nbr.pop();
                continue;
            }
            if (is_branch(br.top()->Branches[cp.top()]))
            {
                br.push(as_branch(br.top()->Branches[cp.top()]));
                cp.top()++;
                cp.push(0);
                auto b_ = std::make_shared<Branch>();
                nbr.top()->Branches.push_back(b_);
                nbr.push(b_);

            }
            else
            {
                cp.top()++;
            }
        }
        eval->Data.push(nbranch);
    }
    static void Reverse(Evaluator* eval)
    {
        eval->RequireTop();
        if (is_value(eval->Data.top()))
        {
            auto v = as_value(eval->Data.top());
            for (int i = 0; i < v->Stored.size() / 2; i++)
                std::swap(v->Stored[i], v->Stored[v->Stored.size() - 1 - i]);
        }
        else
        {
            auto v = as_branch(eval->Data.top());
            for (int i = 0; i < v->Branches.size() / 2; i++)
                std::swap(v->Branches[i], v->Branches[v->Branches.size() - 1 - i]);
        }
    }
    static void Copy(Evaluator* eval)
    {
        eval->RequireTop();
        eval->Data.push(eval->Data.top()->Copy());
    }
    static void Duplicate(Evaluator* eval)
    {
        eval->RequireIntegerTop();
        int c = as_value(eval->Data.top())->ReadAs<int>();
        eval->Data.pop();
        for (int i = 1; i < c; i++)
            eval->Data.push(eval->Data.top()->Copy());
    }
    static void DeepRemove(Evaluator* eval)
    {
        eval->RequireIntegerTop();
        int c = as_value(eval->Data.top())->ReadAs<int>();
        eval->Data.pop();
        eval->RequireTop(c + 1);
        std::stack<sp<BranchBase>> tmp;
        for (int i = 0; i < c; i++)
        {
            tmp.push(eval->Data.top());
            eval->Data.pop();
        }
        eval->Data.pop();
        for (int i = 0; i < c; i++)
        {
            eval->Data.push(tmp.top());
            tmp.pop();
        }
    }
    static void SplitRow(Evaluator* eval)
    {
        eval->RequireValueTop();
        std::string split = as_value(eval->Data.top())->Stored;
        if (split.size() == 0)
            ExecutionEngineException::ThrowWraped("Empty passed as split", ExecutionEngineException::Level::Critical);
        eval->Data.pop();
        eval->RequireValueTop();
        std::string value = as_value(eval->Data.top())->Stored;
        eval->Data.pop();
        sp<Branch> r = std::make_shared<Branch>();
        int p = 0;
        int np = value.find(split, p);
        if (np == std::string::npos)
            np = value.size();
        while (np != value.size())
        {
            r->Branches.push_back(std::make_shared<Value>(value.substr(p, np - p)));
            p = np + split.size();
            np = value.find(split, p);
            if (np == std::string::npos)
                np = value.size();
        }
        r->Branches.push_back(std::make_shared<Value>(value.substr(p, np - p)));
        eval->Data.push(r);
    }
    static void ConcatRow(Evaluator* eval)
    {
        eval->RequireValueTop();
        std::string space = as_value(eval->Data.top())->Stored;
        eval->Data.pop();
        eval->RequireBranchTop();
        sp<Branch> br = as_branch(eval->Data.top());
        eval->Data.pop();
        std::stringstream str;
        bool f = false;
        for (auto& i : br->Branches)
        {
            if (i->Depth() == 0)
            {
                if (!f)
                    f = true;
                else
                    str << space;
                str << as_value(i)->Stored;
            }
        }
        eval->Data.push(std::make_shared<Value>(str.str()));
    }
    static void MergeBranches(Evaluator* eval)
    {
        eval->RequireIntegerTop();
        int depth = as_value(eval->Data.top())->ReadAs<int>();

    }
#pragma endregion
};
#endif EVALCORE_H_HPP
