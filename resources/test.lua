Miata.config.color.background = "#11223344"
Miata.config.color.normal_text = "#aaff55ff"
Miata.config.color.normal_file = "#ffffffff"
Miata.config.color.directory = "#00ffaaff"




print("HOGE")
print("FUGA")
local a = 1
print(a)

Miata.util.pp("HOGE")
Miata.util.pp(Miata)

Miata.command.bind("", "j", function()
    Miata.command.navigate_down()
end)
Miata.command.bind("", "k", function()
    Miata.command.navigate_up()
end)
Miata.command.bind("", "h", function()
    Miata.command.navigate_left()
end)
Miata.command.bind("", "l", function()
    Miata.command.navigate_right()
end)
Miata.command.bind("", "<C-d>", function()
    Miata.command.navigate_down(10)
end)
Miata.command.bind("", "<C-u>", function()
    Miata.command.navigate_up(10)
end)
Miata.command.bind("", "<down>", function()
    Miata.command.navigate_down()
end)
Miata.command.bind("", "<up>", function()
    Miata.command.navigate_up()
end)
Miata.command.bind("", "<left>", function()
    Miata.command.navigate_left()
end)
Miata.command.bind("", "<right>", function()
    Miata.command.navigate_right()
end)
Miata.command.bind("", "<enter>", function()
    Miata.command.navigate_ok()
end)
Miata.command.bind("", "<esc>", function()
    Miata.command.navigate_cancel()
end)
Miata.command.bind("", "<tab>", function()
    Miata.command.toggle_focus()
end)
Miata.command.bind("", " ", function()
    Miata.command.toggle_mark()
    Miata.command.navigate_down()
end)
Miata.command.bind("", "m", function()
    Miata.command.toggle_mark()
    Miata.command.navigate_down()
end)





Miata.command.bind("", "<C-q>s", function()
    print("C-q s")
end)
Miata.command.bind("", "<S-u>", function()
    print("S-u")
end)
Miata.command.bind("", "<S-up>", function()
    print("S-up")
end)

Miata.command.unbind("", "<S-up>")





--return false
