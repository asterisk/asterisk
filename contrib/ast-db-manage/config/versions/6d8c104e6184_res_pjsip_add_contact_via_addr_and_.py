"""res_pjsip: add contact via_addr and callid

Revision ID: 6d8c104e6184
Revises: 81b01a191a46
Create Date: 2016-05-04 17:02:16.355078

"""

# revision identifiers, used by Alembic.
revision = '6d8c104e6184'
down_revision = '81b01a191a46'

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
