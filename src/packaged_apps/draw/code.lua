d = app:draw(300,300)
cl = app:button("clear")
blackb = app:button("black")
redb = app:button("red")
greenb = app:button("green")
blueb = app:button("blue")

app:place_across(d,0,0,1,5)
app:place(cl,1,4)
app:place(blackb, 1,0)
app:place(redb,1,1)
app:place(greenb,1,2)
app:place(blueb,1,3)
app:place(cl,1,4)

d:when_mouse_pressed("pressed")
d:when_mouse_dragged("dragged")
cl:when_clicked("clear()")

redp = app:pen("red",2)
greenp = app:pen("green",2)
bluep = app:pen("blue",2)
blackp = app:pen("black",2)

redb:when_clicked("set_color(redp,'r' )")
greenb:when_clicked("set_color(greenp,'g')")
blueb:when_clicked("set_color(bluep,'b')")
blackb:when_clicked("set_color(blackp,\"bl\")")


px = -1
py = -1

cur_pen = ""
function set_color(p, pt)
	d:pen(p)
	cur_pen = pt
end
set_color(blackp, "bl")

function pressed(b, x, y)
	px = x
	py = y
end

function dragged(b, x, y)
	d:line(px, py, x , y)
	send_line(px, py, x, y,cur_pen)
	px = x
	py = y
end

function clear()
	d:clear()
	send_clear()
end

function get_color(p)
	if p == 'r' then
		return redp
	elseif p == 'g' then
		return greenp
	elseif p == 'b' then
		return bluep
	elseif p == "bl" then
		return blackp
	end
	return blackp
end

app:when_message_received("got")
function got(m)
	local type = m:get("t")
	if type == "l" then
		local ln = m:get("ln")
		local x1, y1, x2, y2,p = ln:match("(%d+) (%d+) (%d+) (%d+) (%l+)")
		local prev_pen = d:get_pen()
		local cp = get_color(p)
		d:pen(cp)
		d:line(x1, y1, x2, y2)
		d:pen(prev_pen)
	elseif type == "cl" then
		d:clear()
	end	
end

function send_line(x1,y1,x2,y2, pen)
	local m = app:message()
	m:set("t","l")
	m:set("ln", x1.." "..y1.." "..x2.." "..y2.." "..pen)
	app:send(m)
end

function send_clear()
	local m = app:message()
	m:set("t", "cl")
	app:send(m)
end

