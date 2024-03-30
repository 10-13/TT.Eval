#include <iostream>
#include "EvalCore.h"

void Print(Evaluator* eval)
{
	eval->PrintData(std::cout);
}
void System(Evaluator* eval)
{
	eval->RequireValueTop();
	std::system(as_value(eval->Data.top())->Stored.c_str());
	eval->Data.pop();
}
void Exit(Evaluator* eval)
{
	std::exit(1);
}

int main()
{
	Evaluator ev{};
	ev.LoadDefault();
	ev.Functions.insert({ Print, "print" });
	ev.Functions.insert({ System, "system" });
	ev.Functions.insert({ Exit, "exit" });
	while (true)
	{
		std::string l;
		std::getline(std::cin, l);
		ev.EvalCom(l);
	}
}