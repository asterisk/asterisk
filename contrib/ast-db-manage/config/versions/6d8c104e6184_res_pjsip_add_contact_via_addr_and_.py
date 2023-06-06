"""res_pjsip: add contact via_addr and callid

Revision ID: a845e4d8ade8
Revises: d7e3c73eb2bf
Create Date: 2016-05-19 15:51:33.410852

"""

# revision identifiers, used by Alembic.
revision = 'a845e4d8ade8'
down_revision = 'd7e3c73eb2bf'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_contacts', sa.Column('via_addr', sa.String(40)))
    op.add_column('ps_contacts', sa.Column('via_port', sa.Integer))
    op.add_column('ps_contacts', sa.Column('call_id', sa.String(255)))

def downgrade():
    op.drop_column('ps_contacts', 'via_addr')
    op.drop_column('ps_contacts', 'via_port')
    op.drop_column('ps_contacts', 'call_id')
